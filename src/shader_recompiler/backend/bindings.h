// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/common_types.h"

namespace Shader::Backend {

struct Bindings {
    u32 unified{};
    u32 uniform_buffer{};
    u32 storage_buffer{};
    u32 texture{};
    u32 image{};
    u32 texture_scaling_index{};
    u32 image_scaling_index{};
    /// Bindless textures: the slot table's binding (~0 until the first stage with textures
    /// declares it; later stages reuse it) and the running count of texture elements, the table
    /// index of the next descriptor's first element.
    u32 texture_table = ~0U;
    u32 texture_element{};
};

} // namespace Shader::Backend
