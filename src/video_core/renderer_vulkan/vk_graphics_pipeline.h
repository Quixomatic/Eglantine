// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

#include "common/thread_worker.h"
#include "shader_recompiler/shader_info.h"
#include "video_core/engines/maxwell_3d.h"
#include "video_core/renderer_vulkan/fixed_pipeline_state.h"
#include "video_core/renderer_vulkan/pipeline_helper.h"
#include "video_core/renderer_vulkan/vk_buffer_cache.h"
#include "video_core/renderer_vulkan/vk_descriptor_buffer.h"
#include "video_core/renderer_vulkan/vk_descriptor_pool.h"
#include "video_core/renderer_vulkan/vk_texture_cache.h"
#include "video_core/renderer_vulkan/vk_update_descriptor.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace VideoCore {
class ShaderNotify;
}

namespace Vulkan {

struct GraphicsPipelineCacheKey {
    std::array<u64, 6> unique_hashes;
    FixedPipelineState state;

    size_t Hash() const noexcept;

    bool operator==(const GraphicsPipelineCacheKey& rhs) const noexcept;

    bool operator!=(const GraphicsPipelineCacheKey& rhs) const noexcept {
        return !operator==(rhs);
    }

    size_t Size() const noexcept {
        return sizeof(unique_hashes) + state.Size();
    }
};
static_assert(std::has_unique_object_representations_v<GraphicsPipelineCacheKey>);
static_assert(std::is_trivially_copyable_v<GraphicsPipelineCacheKey>);
static_assert(std::is_trivially_constructible_v<GraphicsPipelineCacheKey>);

} // namespace Vulkan

namespace std {
template <>
struct hash<Vulkan::GraphicsPipelineCacheKey> {
    size_t operator()(const Vulkan::GraphicsPipelineCacheKey& k) const noexcept {
        return k.Hash();
    }
};
} // namespace std

namespace Vulkan {

/// SPIR-V modules of the graphics stages, null for unused stages. Shared by the pipelines whose
/// keys differ only in state the shaders do not see (ShaderReuseCache).
using GraphicsShaderModules =
    std::array<vk::ShaderModule, Tegra::Engines::Maxwell3D::Regs::MaxShaderStage>;

class Device;
class PipelineStatistics;
class RenderPassCache;
class RescalingPushConstant;
class RenderAreaPushConstant;
class Scheduler;

class GraphicsPipeline {
    static constexpr size_t NUM_STAGES = Tegra::Engines::Maxwell3D::Regs::MaxShaderStage;

public:
    explicit GraphicsPipeline(
        Scheduler& scheduler, BufferCache& buffer_cache, TextureCache& texture_cache,
        vk::PipelineCache& pipeline_cache, VideoCore::ShaderNotify* shader_notify,
        const Device& device, DescriptorPool& descriptor_pool,
        GuestDescriptorQueue& guest_descriptor_queue,
        DescriptorBufferRing& descriptor_buffer_ring, Common::ThreadWorker* worker_thread,
        PipelineStatistics* pipeline_statistics, RenderPassCache& render_pass_cache,
        const GraphicsPipelineCacheKey& key,
        std::shared_ptr<const GraphicsShaderModules> modules,
        const std::array<const Shader::Info*, NUM_STAGES>& infos);

    bool HasDynamicVertexInput() const noexcept { return key.state.dynamic_vertex_input; }
    bool SupportsAlphaToCoverage() const noexcept {
        return fragment_has_color0_output;
    }

    bool SupportsAlphaToOne() const noexcept {
        return fragment_has_color0_output;
    }

    bool UsesExtendedDynamicState() const noexcept {
        return key.state.extended_dynamic_state != 0;
    }

    /// Switch 2 task/mesh pipeline: drawn with vkCmdDrawMeshTasksEXT.
    [[nodiscard]] bool IsMeshPipeline() const noexcept {
        return stage_infos[0].is_task || stage_infos[0].is_mesh;
    }
    GraphicsPipeline& operator=(GraphicsPipeline&&) noexcept = delete;
    GraphicsPipeline(GraphicsPipeline&&) noexcept = delete;

    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(const GraphicsPipeline&) = delete;

    void AddTransition(GraphicsPipeline* transition);

    bool Configure(bool is_indexed) {
        return configure_func(this, is_indexed);
    }

    [[nodiscard]] GraphicsPipeline* Next(const GraphicsPipelineCacheKey& current_key) noexcept {
        if (key == current_key) {
            return this;
        }
        const auto it{std::find(transition_keys.begin(), transition_keys.end(), current_key)};
        return it != transition_keys.end() ? transitions[std::distance(transition_keys.begin(), it)]
                                           : nullptr;
    }

    /// The frame the pipeline cache last handed this pipeline out (evicts long unused ones).
    void MarkUsed(u64 frame) noexcept {
        last_use_frame = frame;
    }

    [[nodiscard]] u64 LastUse() const noexcept {
        return last_use_frame;
    }

    /// Forgets the transitions, which may point to pipelines the cache evicted.
    void ClearTransitions() noexcept {
        transition_keys.clear();
        transitions.clear();
    }

    [[nodiscard]] bool IsBuilt() const noexcept {
        return is_built.load(std::memory_order::relaxed);
    }

    [[nodiscard]] bool WritesStorage() const noexcept {
        return writes_storage;
    }

    /// Writes storage images or image buffers (their images are marked modified when resolved).
    [[nodiscard]] bool WritesImages() const noexcept {
        return writes_images;
    }

    /// EDEN_NX2_FAST_DRAW=2 (nx2_fast_draw.h): whether draws can keep this pipeline's resolved
    /// image views and samplers after light changes. Handles computed at draw time are resolved
    /// on every draw.
    [[nodiscard]] bool TakesDeltaDraws() const noexcept {
        return delta_blocker == DeltaBlocker::None && delta_views_valid;
    }

    /// Why TakesDeltaDraws is false (diagnostics).
    enum class DeltaBlocker : u8 {
        None,
        WritesImages,
        Mesh,
        ComputedHandles,
        NotConfigured,
        Bindless, ///< Ours: bindless pipelines push a slot table, not image descriptors
        Count,
    };
    [[nodiscard]] DeltaBlocker DeltaDrawBlocker() const noexcept {
        return delta_blocker != DeltaBlocker::None ? delta_blocker
               : delta_views_valid                 ? DeltaBlocker::None
                                                   : DeltaBlocker::NotConfigured;
    }

    /// Constant buffer slots per stage the pipeline reads texture and image handles from.
    [[nodiscard]] const std::array<u32, NUM_STAGES>& HandleConstantBuffers() const noexcept {
        return handle_constant_buffers;
    }

    enum class DeltaResult { Configured, Failed, NeedsFull };
    /// Binds the constant and storage buffers again (storage buffer addresses are read from
    /// constant buffers) and records the draw's descriptors with the image views, samplers and
    /// texture buffers the last full Configure resolved.
    DeltaResult ConfigureDelta();

    /// EDEN_NX2_FAST_DRAW_VERIFY: the next Configure compares what it resolves with the views
    /// and samplers a delta draw would have kept; DeltaMismatch reports the result.
    void VerifyNextDelta() noexcept {
        verify_delta = true;
    }
    [[nodiscard]] bool DeltaMismatch() const noexcept {
        return delta_mismatch;
    }

    template <typename Spec>
    static auto MakeConfigureSpecFunc() {
        return [](GraphicsPipeline* pl, bool is_indexed) { return pl->ConfigureImpl<Spec>(is_indexed); };
    }

    void SetEngine(Tegra::Engines::Maxwell3D* maxwell3d_, Tegra::MemoryManager* gpu_memory_) {
        maxwell3d = maxwell3d_;
        gpu_memory = gpu_memory_;
    }

private:
    template <typename Spec>
    bool ConfigureImpl(bool is_indexed);

public:
    /// Whether a stage reads the draw index builtin (gl_DrawID): its draws are never merged.
    [[nodiscard]] bool ReadsDrawIndex() const noexcept {
        return reads_draw_index;
    }

private:
    bool reads_draw_index{};

    /// Bindless: pushes the slot table's descriptor (the table's stage, in its texture slot).
    void PushBindlessTable(size_t stage);

    bool ConfigureDraw(const RescalingPushConstant& rescaling,
                       const RenderAreaPushConstant& render_are);

    void MakePipeline(VkRenderPass render_pass);

    void Validate();

    const GraphicsPipelineCacheKey key;
    Tegra::Engines::Maxwell3D* maxwell3d;
    Tegra::MemoryManager* gpu_memory;
    const Device& device;
    TextureCache& texture_cache;
    BufferCache& buffer_cache;
    vk::PipelineCache& pipeline_cache;
    Scheduler& scheduler;
    GuestDescriptorQueue& guest_descriptor_queue;
    DescriptorBufferRing& descriptor_buffer_ring;

    bool (*configure_func)(GraphicsPipeline*, bool){};

    std::vector<GraphicsPipelineCacheKey> transition_keys;
    std::vector<GraphicsPipeline*> transitions;
    u64 last_use_frame{};

    std::shared_ptr<const GraphicsShaderModules> spv_modules;

    std::array<Shader::Info, NUM_STAGES> stage_infos;
    std::array<u32, 5> enabled_uniform_buffer_masks{};
    VideoCommon::UniformBufferSizes uniform_buffer_sizes{};
    u32 num_descriptor_entries{};
    size_t num_image_elements{};
    u32 num_textures{};
    bool fragment_has_color0_output{};
    bool writes_storage{};
    bool writes_images{};

    DeltaBlocker delta_blocker{};
    bool delta_views_valid{};
    bool verify_delta{};
    bool delta_mismatch{};
    std::array<bool, NUM_STAGES> configured_stages{};
    std::array<u32, NUM_STAGES> handle_constant_buffers{};
    std::vector<VideoCommon::ImageViewInOut> delta_views;
    std::vector<VideoCommon::SamplerId> delta_samplers;

    vk::DescriptorSetLayout descriptor_set_layout;
    DescriptorAllocator descriptor_allocator;
    vk::PipelineLayout pipeline_layout;
    vk::DescriptorUpdateTemplate descriptor_update_template;
    vk::Pipeline pipeline;

    DescriptorBufferLayout descriptor_buffer_layout;
    std::vector<DescriptorUpdateEntry> last_descriptor_payload;
    /// What the last recorded draw of this pipeline bound (C: an identical next draw records
    /// nothing while that draw is still pending in the scheduler).
    const DescriptorUpdateEntry* recorded_payload{};
    std::remove_cvref_t<decltype(std::declval<RescalingPushConstant>().Data())> recorded_rescaling{};
    std::array<f32, 4> recorded_render_area{};
    bool recorded_render_area_used{};
    VkDeviceSize recorded_offset{};

    /// Binding cache, texture side: the last full resolution's image views and samplers and the
    /// values they came from (each handle word and its 32-byte TIC and TSC entries), with host
    /// pointers to them. Reused while the values compare equal: DK rewrites these every frame with
    /// the same contents, so they are compared, not write-tracked; the pointers skip the address
    /// translation a full resolution does for every read.
    struct TextureSlotCache {
        GPUVAddr handle_address;
        const u8* handle_host;
        u32 handle_raw;
        const u8* tic_host;
        const u8* tsc_host;
        std::array<u8, 32> tic;
        std::array<u8, 32> tsc;
    };
    struct TextureBindingCache {
        bool valid{};
        bool via_header_index{};
        u64 gpu_map_serial{};
        u64 device_map_serial{};
        u64 texture_serial{};
        std::vector<TextureSlotCache> slots;
        std::vector<VideoCommon::ImageViewInOut> views;
        std::vector<VideoCommon::SamplerId> samplers;
        /// The descriptor entries these views and samplers produced (image view and sampler
        /// handles, and per texture descriptor whether it is rescaled), pushed again on a hit:
        /// they change only with what invalidates the cache (deletion, rescaling).
        bool entries_valid{};
        std::vector<std::pair<VkImageView, VkSampler>> image_entries;
        std::vector<u8> rescaled;
    };
    TextureBindingCache texture_binding_cache;

    /// Bindless textures (see .plans/bindless-design.md): the heap, null when this pipeline binds
    /// its textures the classic way; the stage whose texture slot holds the slot table; the table
    /// words of the last draw and the stream allocation they were uploaded to.
    TextureHeap* texture_heap{};
    size_t bindless_table_stage{};
    std::vector<u32> bindless_words;
    std::vector<u8> bindless_rescaled;
    bool bindless_words_valid{};
    std::vector<u32> bindless_uploaded_words;
    BufferCacheRuntime::StreamUniformRef bindless_upload_ref{};
    u64 bindless_upload_tick{~0ULL};
    /// -1 not computed, 0 no, 1 yes: only plain sampled textures with constant buffer handles.
    s8 texture_cache_eligible{-1};
    VkDeviceSize last_descriptor_buffer_offset{};
    u32 last_descriptor_buffer_chunk{};
    u64 last_descriptor_buffer_generation{};

    std::condition_variable build_condvar;
    std::mutex build_mutex;
    std::atomic_bool is_built{false};
    bool uses_push_descriptor{false};
    bool uses_descriptor_buffer{false};
};

} // namespace Vulkan
