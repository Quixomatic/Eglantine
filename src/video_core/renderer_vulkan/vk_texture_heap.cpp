// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <vector>

#include "common/logging.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_texture_heap.h"
#include "video_core/vulkan_common/vulkan_device.h"

namespace Vulkan {

namespace Bindless = Shader::Bindless;

namespace {
constexpr u32 kMaxImagesPerClass = 16384;
constexpr u32 kMaxSamplers = 4096;
constexpr VkDescriptorBindingFlags kBindingFlags =
    VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
    VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
} // Anonymous namespace

TextureHeap::TextureHeap(const Device& device_, Scheduler& scheduler_)
    : device{device_}, scheduler{scheduler_} {
    const auto& limits = device.DescriptorIndexingProperties();
    // Every array is visible to every stage, so the per-stage limits bound the sum of all arrays.
    image_capacity = std::min({kMaxImagesPerClass,
                               limits.maxPerStageDescriptorUpdateAfterBindSampledImages /
                                   Bindless::kNumImageClasses,
                               limits.maxDescriptorSetUpdateAfterBindSampledImages /
                                   Bindless::kNumImageClasses,
                               Bindless::kMaxImageSlots});
    sampler_capacity = std::min({kMaxSamplers, limits.maxPerStageDescriptorUpdateAfterBindSamplers,
                                 limits.maxDescriptorSetUpdateAfterBindSamplers,
                                 Bindless::kMaxSamplerSlots});

    std::array<VkDescriptorSetLayoutBinding, Bindless::kNumImageClasses + 1> bindings{};
    std::array<VkDescriptorBindingFlags, Bindless::kNumImageClasses + 1> binding_flags{};
    for (u32 binding = 0; binding < bindings.size(); ++binding) {
        const bool is_sampler = binding == Bindless::kSamplerBinding;
        bindings[binding] = VkDescriptorSetLayoutBinding{
            .binding = binding,
            .descriptorType = is_sampler ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = is_sampler ? sampler_capacity : image_capacity,
            .stageFlags = VK_SHADER_STAGE_ALL,
            .pImmutableSamplers = nullptr,
        };
        binding_flags[binding] = kBindingFlags;
    }
    const VkDescriptorSetLayoutBindingFlagsCreateInfo flags_ci{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .pNext = nullptr,
        .bindingCount = static_cast<u32>(binding_flags.size()),
        .pBindingFlags = binding_flags.data(),
    };
    layout = device.GetLogical().CreateDescriptorSetLayout(VkDescriptorSetLayoutCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flags_ci,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    });

    const std::array pool_sizes{
        VkDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            .descriptorCount = image_capacity * Bindless::kNumImageClasses,
        },
        VkDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_SAMPLER,
            .descriptorCount = sampler_capacity,
        },
    };
    pool = device.GetLogical().CreateDescriptorPool(VkDescriptorPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .maxSets = 1,
        .poolSizeCount = static_cast<u32>(pool_sizes.size()),
        .pPoolSizes = pool_sizes.data(),
    });
    const VkDescriptorSetLayout layout_handle = *layout;
    sets = pool.Allocate(VkDescriptorSetAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = nullptr,
        .descriptorPool = *pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout_handle,
    });
    if (sets.IsOutOfPoolMemory() || sets.size() != 1) {
        throw vk::Exception(VK_ERROR_OUT_OF_POOL_MEMORY);
    }
    set = sets[0];

    // Slot 0 of every array: a null descriptor (robustness2 nullDescriptor is required).
    for (u32 image_class = 0; image_class < Bindless::kNumImageClasses; ++image_class) {
        WriteImage(image_class, 0, VK_NULL_HANDLE);
    }
    null_sampler = device.GetLogical().CreateSampler(VkSamplerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_NEVER,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    });
    WriteSampler(0, *null_sampler);
    LOG_INFO(Render_Vulkan, "Bindless texture heap: {} image slots per class x {}, {} samplers",
             image_capacity, Bindless::kNumImageClasses, sampler_capacity);
}

TextureHeap::~TextureHeap() = default;

u32 TextureHeap::TakeSlot(Pool& slot_pool, u32 capacity, const char* what) {
    if (!slot_pool.released.empty() && scheduler.IsFree(slot_pool.released.front().second)) {
        const u32 slot = slot_pool.released.front().first;
        slot_pool.released.pop_front();
        return slot;
    }
    if (slot_pool.next_unused < capacity) {
        return slot_pool.next_unused++;
    }
    if (!slot_pool.reported_full) {
        slot_pool.reported_full = true;
        LOG_ERROR(Render_Vulkan, "Bindless texture heap: {} slots exhausted ({}); using null",
                  what, capacity);
    }
    return 0;
}

u32 TextureHeap::AllocateImage(u32 image_class, VkImageView view) {
    if (view == VK_NULL_HANDLE || image_class >= Bindless::kNumImageClasses) {
        return 0;
    }
    std::scoped_lock lock{mutex};
    const u32 slot = TakeSlot(image_pools[image_class], image_capacity, "image");
    if (slot != 0) {
        WriteImage(image_class, slot, view);
    }
    return slot;
}

void TextureHeap::ReleaseImage(u32 image_class, u32 slot) {
    if (slot == 0 || image_class >= Bindless::kNumImageClasses) {
        return;
    }
    std::scoped_lock lock{mutex};
    image_pools[image_class].released.emplace_back(slot, scheduler.CurrentTick());
}

u32 TextureHeap::SamplerSlot(VkSampler sampler) {
    if (sampler == VK_NULL_HANDLE) {
        return 0;
    }
    std::scoped_lock lock{mutex};
    const auto [it, is_new] = sampler_slots.try_emplace(sampler, 0);
    if (is_new) {
        it->second = TakeSlot(sampler_pool, sampler_capacity, "sampler");
        if (it->second != 0) {
            WriteSampler(it->second, sampler);
        }
    }
    return it->second;
}

void TextureHeap::WriteImage(u32 image_class, u32 slot, VkImageView view) {
    const VkDescriptorImageInfo image_info{
        .sampler = VK_NULL_HANDLE,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = set,
        .dstBinding = image_class,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &image_info,
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    };
    device.GetLogical().UpdateDescriptorSets(write, {});
}

void TextureHeap::WriteSampler(u32 slot, VkSampler sampler) {
    const VkDescriptorImageInfo image_info{
        .sampler = sampler,
        .imageView = VK_NULL_HANDLE,
        .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .pNext = nullptr,
        .dstSet = set,
        .dstBinding = Bindless::kSamplerBinding,
        .dstArrayElement = slot,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .pImageInfo = &image_info,
        .pBufferInfo = nullptr,
        .pTexelBufferView = nullptr,
    };
    device.GetLogical().UpdateDescriptorSets(write, {});
}

} // namespace Vulkan
