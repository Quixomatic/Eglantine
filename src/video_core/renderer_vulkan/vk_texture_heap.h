// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "common/common_types.h"
#include "shader_recompiler/bindless.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class Scheduler;

/// The bindless texture heap: one update-after-bind descriptor set holding an array of sampled
/// images per image class (Shader::Bindless::ImageClass) and an array of samplers. Shaders of
/// bindless pipelines index it with slots from a per-draw table. See .plans/bindless-design.md.
///
/// Slot 0 of every array is a null descriptor. A slot's descriptor is written when it is
/// allocated, before any command using it is recorded (update-after-bind and
/// update-unused-while-pending make that legal while the set is bound). Released slots are reused
/// only once the GPU has passed the tick they were released at, so a descriptor is never rewritten
/// while a submitted command buffer may read it. Thread safe.
class TextureHeap {
public:
    explicit TextureHeap(const Device& device, Scheduler& scheduler);
    ~TextureHeap();

    TextureHeap(const TextureHeap&) = delete;
    TextureHeap& operator=(const TextureHeap&) = delete;

    [[nodiscard]] VkDescriptorSetLayout Layout() const noexcept {
        return *layout;
    }

    [[nodiscard]] VkDescriptorSet Set() const noexcept {
        return set;
    }

    /// Allocates a slot of `image_class` holding `view` (VK_NULL_HANDLE gives slot 0). Returns 0
    /// when the class is full (logged once): the draw then samples a null texture.
    [[nodiscard]] u32 AllocateImage(u32 image_class, VkImageView view);

    /// Returns an image slot; it is reused after the GPU passes the current tick.
    void ReleaseImage(u32 image_class, u32 slot);

    /// The slot holding `sampler`, allocated on first use. The texture cache never destroys
    /// samplers, so a sampler keeps its slot for the heap's lifetime.
    [[nodiscard]] u32 SamplerSlot(VkSampler sampler);

    [[nodiscard]] u32 ImageCapacity() const noexcept {
        return image_capacity;
    }

private:
    struct Pool {
        u32 next_unused = 1;
        std::deque<std::pair<u32, u64>> released; ///< (slot, release tick), oldest first
        bool reported_full = false;
    };

    u32 TakeSlot(Pool& pool, u32 capacity, const char* what);
    void WriteImage(u32 image_class, u32 slot, VkImageView view);
    void WriteSampler(u32 slot, VkSampler sampler);

    const Device& device;
    Scheduler& scheduler;
    vk::DescriptorSetLayout layout;
    vk::DescriptorPool pool;
    vk::DescriptorSets sets;
    VkDescriptorSet set{};
    vk::Sampler null_sampler; ///< Sampler slot 0 (a null sampler descriptor is not allowed)
    u32 image_capacity = 0;
    u32 sampler_capacity = 0;

    std::mutex mutex;
    std::array<Pool, Shader::Bindless::kNumImageClasses> image_pools;
    Pool sampler_pool;
    std::unordered_map<VkSampler, u32> sampler_slots;
};

/// An image view's heap slots, one per image class, allocated on first use and released when
/// the view is destroyed. Moving transfers the slots (SlotVector relocates views by moving them).
class TextureHeapSlots {
public:
    TextureHeapSlots() = default;
    ~TextureHeapSlots() {
        Release();
    }

    TextureHeapSlots(const TextureHeapSlots&) = delete;
    TextureHeapSlots& operator=(const TextureHeapSlots&) = delete;

    TextureHeapSlots(TextureHeapSlots&& rhs) noexcept
        : heap{std::exchange(rhs.heap, nullptr)}, slots{std::exchange(rhs.slots, {})} {}

    TextureHeapSlots& operator=(TextureHeapSlots&& rhs) noexcept {
        if (this != &rhs) {
            Release();
            heap = std::exchange(rhs.heap, nullptr);
            slots = std::exchange(rhs.slots, {});
        }
        return *this;
    }

    /// The view's slot in `image_class`, allocating it with `view` the first time.
    [[nodiscard]] u32 Get(TextureHeap& owner, u32 image_class, VkImageView view) {
        u32& slot = slots[image_class];
        if (slot == 0 && view != VK_NULL_HANDLE) {
            heap = &owner;
            slot = owner.AllocateImage(image_class, view);
        }
        return slot;
    }

private:
    void Release() noexcept {
        if (heap == nullptr) {
            return;
        }
        for (u32 image_class = 0; image_class < slots.size(); ++image_class) {
            if (slots[image_class] != 0) {
                heap->ReleaseImage(image_class, slots[image_class]);
            }
        }
        heap = nullptr;
        slots = {};
    }

    TextureHeap* heap = nullptr;
    std::array<u32, Shader::Bindless::kNumImageClasses> slots{};
};

} // namespace Vulkan
