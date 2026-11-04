// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "shader_recompiler/shader_info.h"

// Bindless textures: the layout contract between the SPIR-V backend and the Vulkan texture heap.
// A bindless shader samples textures from heap arrays in descriptor set kHeapSet, one array per
// image class (SPIR-V needs one image type per array), indexed by a per-draw slot table.
namespace Shader::Bindless {

/// The heap's descriptor set; a bindless pipeline's own resources are in kResourceSet.
inline constexpr u32 kHeapSet = 0;
inline constexpr u32 kResourceSet = 1;

/// Image classes: dimension x (float colour, uint colour, multisample float/uint, depth).
enum class Dim : u32 { D1, D1Array, D2, D2Array, D3, Cube, CubeArray };
inline constexpr u32 kNumDims = 7;
inline constexpr u32 kColorFloatBase = 0;            // 7 classes
inline constexpr u32 kColorUintBase = kNumDims;      // 7 classes
inline constexpr u32 kMultisampleBase = 2 * kNumDims; // 2D/2DArray x float/uint: 4 classes
inline constexpr u32 kDepthBase = kMultisampleBase + 4; // 1D, 1DArray, 2D, 2DArray, Cube, CubeArray
inline constexpr u32 kNumImageClasses = kDepthBase + 6;
/// The heap binding of the sampler array (image class c is binding c).
inline constexpr u32 kSamplerBinding = kNumImageClasses;

/// Slot table word: image slot in the low bits, sampler slot above.
inline constexpr u32 kImageSlotBits = 18;
inline constexpr u32 kImageSlotMask = (1u << kImageSlotBits) - 1;
inline constexpr u32 kMaxImageSlots = 1u << kImageSlotBits;
inline constexpr u32 kMaxSamplerSlots = 1u << (32 - kImageSlotBits);
/// The slot table is a UBO of kTableVectors uvec4 (16 KiB, the guaranteed uniform range).
inline constexpr u32 kTableVectors = 1024;
inline constexpr u32 kMaxTableElements = kTableVectors * 4;

[[nodiscard]] constexpr u32 PackSlots(u32 image_slot, u32 sampler_slot) noexcept {
    return (image_slot & kImageSlotMask) | (sampler_slot << kImageSlotBits);
}

[[nodiscard]] constexpr Dim DimOf(TextureType type) noexcept {
    switch (type) {
    case TextureType::Color1D:
        return Dim::D1;
    case TextureType::ColorArray1D:
        return Dim::D1Array;
    case TextureType::ColorArray2D:
        return Dim::D2Array;
    case TextureType::Color3D:
        return Dim::D3;
    case TextureType::ColorCube:
        return Dim::Cube;
    case TextureType::ColorArrayCube:
        return Dim::CubeArray;
    case TextureType::Color2D:
    case TextureType::Color2DRect:
    case TextureType::Buffer:
    default:
        return Dim::D2;
    }
}

/// The view type a class's descriptors are written with (Color2DRect samples a 2D view).
[[nodiscard]] constexpr TextureType ViewTypeOf(TextureType type) noexcept {
    return type == TextureType::Color2DRect ? TextureType::Color2D : type;
}

/// The heap array a sampled texture lives in. Buffer textures are never bindless.
[[nodiscard]] constexpr u32 ImageClass(TextureType type, bool is_depth, bool is_multisample,
                                       bool is_integer) noexcept {
    const Dim dim = DimOf(type);
    const u32 dim_index = static_cast<u32>(dim);
    if (is_multisample && (dim == Dim::D2 || dim == Dim::D2Array)) {
        return kMultisampleBase + (dim == Dim::D2Array ? 1 : 0) + (is_integer ? 2 : 0);
    }
    if (is_depth && !is_integer && dim != Dim::D3) {
        // 1D, 1DArray, 2D, 2DArray, Cube, CubeArray -> 0..5 (3D has no depth form).
        return kDepthBase + (dim_index < 4 ? dim_index : dim_index - 1);
    }
    return (is_integer ? kColorUintBase : kColorFloatBase) + dim_index;
}

/// What the SPIR-V image type of a class is built from.
struct ClassInfo {
    Dim dim;
    bool is_depth;
    bool is_multisample;
    bool is_integer;
};

[[nodiscard]] constexpr ClassInfo ClassInfoOf(u32 image_class) noexcept {
    if (image_class >= kDepthBase) {
        const u32 index = image_class - kDepthBase;
        return {static_cast<Dim>(index < 4 ? index : index + 1), true, false, false};
    }
    if (image_class >= kMultisampleBase) {
        const u32 index = image_class - kMultisampleBase;
        return {(index & 1) != 0 ? Dim::D2Array : Dim::D2, false, true, (index & 2) != 0};
    }
    if (image_class >= kColorUintBase) {
        return {static_cast<Dim>(image_class - kColorUintBase), false, false, true};
    }
    return {static_cast<Dim>(image_class), false, false, false};
}

} // namespace Shader::Bindless
