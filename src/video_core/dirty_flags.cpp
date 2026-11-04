// SPDX-FileCopyrightText: Copyright 2019 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>

#include "common/common_types.h"
#include "video_core/dirty_flags.h"

#define OFF(field_name) MAXWELL3D_REG_INDEX(field_name)
#define NUM(field_name) (sizeof(::Tegra::Engines::Maxwell3D::Regs::field_name) / (sizeof(u32)))

namespace VideoCommon::Dirty {
namespace {
using Tegra::Engines::Maxwell3D;

void SetupDirtyVertexBuffers(Maxwell3D::DirtyState::Tables& tables) {
    static constexpr std::size_t num_array = 3;
    for (std::size_t i = 0; i < Maxwell3D::Regs::NumVertexArrays; ++i) {
        const std::size_t array_offset = OFF(vertex_streams) + i * NUM(vertex_streams[0]);
        const std::size_t limit_offset =
            OFF(vertex_stream_limits) + i * NUM(vertex_stream_limits[0]);

        FillBlock(tables, array_offset, num_array, VertexBuffer0 + i, VertexBuffers);
        FillBlock(tables, 0x600 / sizeof(u32) + i * 2, 2, VertexBuffer0 + i, VertexBuffers);
        FillBlock(tables, limit_offset, NUM(vertex_stream_limits), VertexBuffer0 + i,
                  VertexBuffers);
    }
}

void SetupIndexBuffer(Maxwell3D::DirtyState::Tables& tables) {
    FillBlock(tables[0], OFF(index_buffer), NUM(index_buffer), IndexBuffer);
}

void SetupDirtyDescriptors(Maxwell3D::DirtyState::Tables& tables) {
    FillBlock(tables[0], OFF(tex_header), NUM(tex_header), Descriptors);
    FillBlock(tables[0], OFF(tex_sampler), NUM(tex_sampler), Descriptors);
}

void SetupDirtyRenderTargets(Maxwell3D::DirtyState::Tables& tables) {
    static constexpr std::size_t num_per_rt = NUM(rt[0]);
    static constexpr std::size_t begin = OFF(rt);
    static constexpr std::size_t num = num_per_rt * Maxwell3D::Regs::NumRenderTargets;
    for (std::size_t rt = 0; rt < Maxwell3D::Regs::NumRenderTargets; ++rt) {
        FillBlock(tables[0], begin + rt * num_per_rt, num_per_rt, ColorBuffer0 + rt);
    }
    FillBlock(tables[1], begin, num, RenderTargets);
    FillBlock(tables[0], OFF(surface_clip), NUM(surface_clip), RenderTargets);

    tables[0][OFF(rt_control)] = RenderTargets;
    tables[1][OFF(rt_control)] = RenderTargetControl;

    static constexpr std::array zeta_flags{ZetaBuffer, RenderTargets};
    for (std::size_t i = 0; i < std::size(zeta_flags); ++i) {
        const u8 flag = zeta_flags[i];
        auto& table = tables[i];
        table[OFF(zeta_enable)] = flag;
        table[OFF(zeta_size.width)] = flag;
        table[OFF(zeta_size.height)] = flag;
        FillBlock(table, OFF(zeta), NUM(zeta), flag);
    }
}

void SetupDirtyPipelineKey(Maxwell3D::DirtyState::Tables& tables) {
    // Every register marks the pipeline key dirty, except per-draw data that the key never
    // reads. A register missing from this list only costs a key refresh; one wrongly in it would
    // reuse a stale pipeline (EDEN_NX2_KEY_GATE_VERIFY=1 checks every reuse).
    auto& table = tables[2];
    table.fill(PipelineKey);
    const auto clear = [&](std::size_t begin, std::size_t num) {
        for (std::size_t i = begin; i < begin + num && i < table.size(); ++i) {
            table[i] = NullEntry;
        }
    };
    clear(OFF(const_buffer), NUM(const_buffer));
    clear(OFF(bind_groups), NUM(bind_groups));
    clear(OFF(draw), NUM(draw));
    clear(OFF(index_buffer), NUM(index_buffer));
    clear(OFF(index_buffer32_first), NUM(index_buffer32_first));
    clear(OFF(index_buffer16_first), NUM(index_buffer16_first));
    clear(OFF(index_buffer8_first), NUM(index_buffer8_first));
    clear(OFF(index_buffer32_subsequent), NUM(index_buffer32_subsequent));
    clear(OFF(index_buffer16_subsequent), NUM(index_buffer16_subsequent));
    clear(OFF(index_buffer8_subsequent), NUM(index_buffer8_subsequent));
    clear(OFF(vertex_buffer), NUM(vertex_buffer));
    clear(OFF(global_base_vertex_index), NUM(global_base_vertex_index));
    clear(OFF(global_base_instance_index), NUM(global_base_instance_index));
    clear(OFF(report_semaphore), NUM(report_semaphore));
    clear(OFF(shadow_ram_control), NUM(shadow_ram_control));
    clear(OFF(load_mme), NUM(load_mme));
    for (std::size_t i = 0; i < Maxwell3D::Regs::NumVertexArrays; ++i) {
        // The address words of each stream (its stride and frequency can be pipeline state).
        clear(OFF(vertex_streams) + i * NUM(vertex_streams[0]) + 1, 2);
        clear(OFF(vertex_stream_limits) + i * NUM(vertex_stream_limits[0]),
              NUM(vertex_stream_limits[0]));
        clear(0x600 / sizeof(u32) + i * 2, 2);
    }
}

void SetupDirtyShaders(Maxwell3D::DirtyState::Tables& tables) {
    FillBlock(tables[0], OFF(pipelines), NUM(pipelines[0]) * Maxwell3D::Regs::MaxShaderProgram,
              Shaders);
    // Task and mesh programs are translated with the mesh registers.
    FillBlock(tables[1], Maxwell3D::Regs::MeshControl, 4, Shaders);
}
} // Anonymous namespace

void SetupDirtyFlags(Maxwell3D::DirtyState::Tables& tables) {
    SetupDirtyVertexBuffers(tables);
    SetupIndexBuffer(tables);
    SetupDirtyDescriptors(tables);
    SetupDirtyRenderTargets(tables);
    SetupDirtyShaders(tables);
    SetupDirtyPipelineKey(tables);
}

} // namespace VideoCommon::Dirty
