// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <span>
#include <type_traits>

#include "common/common_types.h"
#include "video_core/nx2_profile.h"

namespace VideoCommon {

/// What a buffer cache runtime offers for stream uniform reuse.
///  - StreamUniformRef: a small trivially copyable description of one stream allocation (the
///    host buffer, its offset and whatever else the runtime needs to add it to a descriptor
///    payload again, e.g. the device address of the descriptor buffer path).
///  - LastStreamUniformRef(): the allocation made by the latest BindMappedUniformBuffer call.
///  - RebindStreamUniform(ref, size): adds `ref` to the descriptor payload exactly as
///    BindMappedUniformBuffer added the allocation the first time.
template <class R>
concept StreamUniformReusable = requires(R& runtime, u32 size) {
    typename R::StreamUniformRef;
    requires std::is_trivially_copyable_v<typename R::StreamUniformRef>;
    { runtime.LastStreamUniformRef() } -> std::same_as<typename R::StreamUniformRef>;
    runtime.RebindStreamUniform(runtime.LastStreamUniformRef(), size);
    { runtime.CurrentTick() } -> std::convertible_to<u64>;
};

/// EDEN_NX2_UBOREUSE=0 turns the reuse off (every small uniform buffer is copied, as before).
[[nodiscard]] inline bool UniformStreamReuseEnabled() noexcept {
    static const bool enabled = [] {
        const char* const value = std::getenv("EDEN_NX2_UBOREUSE");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

template <class Ref>
class UniformStreamReuse {
public:
    static constexpr u32 MaxBytes = 4096; // BufferCache's DEFAULT_SKIP_CACHE_SIZE
    static constexpr size_t NumStages = 5;
    static constexpr size_t NumSlots = 18; // Maxwell constant buffers per stage

    struct Slot {
        Ref ref{};
        u64 tick{};
        u32 size{}; // 0: nothing streamed yet
        u64 device_addr{};        // The previous bind's address
        bool bytes_valid{};       // `bytes` holds that address's bytes (see BindSmallUniformReusing)
        u32 stream_generation{};  // Profiling only: g_stream_write_generation at the previous bind
        // A CPU copy on purpose: the stream allocation is upload (write-combined) memory, and
        // comparing against it instead made reads so slow DK Bananza ran at 1.7 fps.
        std::array<u8, MaxBytes> bytes{};
    };

    void Reset() noexcept {
        for (auto& stage : slots) {
            for (Slot& slot : stage) {
                slot.size = 0;
                slot.tick = 0;
                slot.bytes_valid = false;
            }
        }
    }

    std::array<std::array<Slot, NumSlots>, NumStages> slots{};
    std::array<u8, MaxBytes> scratch{};
};

template <class Ref>
[[nodiscard]] inline UniformStreamReuse<Ref>& UniformStreamReuseState() noexcept {
    static UniformStreamReuse<Ref> state;
    return state;
}

/// Forgets every slot. Call it when a buffer cache (and so its runtime, scheduler and stream
/// buffer) is created: ticks restart with a new scheduler and the old references are dead.
template <class Runtime>
inline void ResetUniformStreamReuse() noexcept {
    if constexpr (StreamUniformReusable<Runtime>) {
        UniformStreamReuseState<typename Runtime::StreamUniformRef>().Reset();
    }
}

/// Binds the small uniform buffer at `device_addr` for (stage, cbuf `index`) through the stream
/// buffer, reusing the slot's previous allocation when the bytes and the command buffer are the
/// same. Returns false (nothing bound) when the buffer does not qualify, so the caller binds it
/// the classic way.
template <class Runtime, class DeviceMemory>
[[nodiscard]] bool BindSmallUniformReusing(Runtime& runtime, DeviceMemory& device_memory,
                                           size_t stage, u32 index, u32 binding_index,
                                           u64 device_addr, u32 size) {
    using Reuse = UniformStreamReuse<typename Runtime::StreamUniformRef>;
    if (size == 0 || size > Reuse::MaxBytes || stage >= Reuse::NumStages ||
        index >= Reuse::NumSlots) {
        return false;
    }
    Reuse& reuse = UniformStreamReuseState<typename Runtime::StreamUniformRef>();
    typename Reuse::Slot& slot = reuse.slots[stage][index];
    // Compare and copy straight from guest memory when the range is contiguous in host memory
    // (the usual case): staging every buffer in scratch first was one more full pass over the
    // bytes for every bound uniform buffer of every draw.
    const u8* source = nullptr;
    if constexpr (requires { device_memory.template GetPointer<u8>(device_addr); }) {
        source = device_memory.template GetPointer<u8>(device_addr);
        // Within one 4 KiB device page the range is contiguous; check the end only otherwise.
        constexpr u64 device_page_mask = 0xFFF;
        if (source != nullptr && (device_addr & device_page_mask) + size > device_page_mask + 1 &&
            device_memory.template GetPointer<u8>(device_addr + size - 1) != source + size - 1) {
            source = nullptr;
        }
    }
    if (source == nullptr) {
        u8* const scratch = reuse.scratch.data();
        device_memory.ReadBlockUnsafe(device_addr, scratch, size);
        source = scratch;
    }

    // A slot rebound to another address almost never has the same bytes (per-object data: DK binds
    // 24% of its uniform buffers this way). Compare only against the same address, and keep the
    // CPU copy only once an address comes back, so those binds copy their bytes once, not twice.
    const bool same_address = slot.device_addr == device_addr && slot.size == size;
    const bool same_bytes = same_address && slot.bytes_valid &&
                            std::memcmp(slot.bytes.data(), source, size) == 0;
    if (Nx2Profile::Enabled()) [[unlikely]] {
        if (!same_address) {
            Nx2Profile::Add(Nx2Profile::Counter::UniformNewAddress, 1);
        } else if (Nx2Profile::LastStreamWrite(device_addr, size) > slot.stream_generation) {
            Nx2Profile::Add(Nx2Profile::Counter::UniformStreamWritten, 1);
        } else if (same_bytes) {
            Nx2Profile::Add(Nx2Profile::Counter::UniformClean, 1);
        } else if (slot.bytes_valid) {
            Nx2Profile::Add(Nx2Profile::Counter::UniformUnexplained, 1);
        }
        slot.stream_generation = Nx2Profile::g_stream_write_generation;
    }
    if (same_bytes && slot.tick == runtime.CurrentTick()) {
        runtime.RebindStreamUniform(slot.ref, size);
        Nx2Profile::Add(Nx2Profile::Counter::UniformReused, 1);
        return true;
    }
    Nx2Profile::Add(Nx2Profile::Counter::UniformCopied, 1);
    const std::span<u8> span = runtime.BindMappedUniformBuffer(stage, binding_index, size);
    std::memcpy(span.data(), source, size);
    slot.ref = runtime.LastStreamUniformRef();
    // After the allocation: the tick the stream region is tagged with is the one current now.
    slot.tick = runtime.CurrentTick();
    if (!same_address) {
        slot.device_addr = device_addr;
        slot.size = size;
        slot.bytes_valid = false;
    } else if (!same_bytes) {
        std::memcpy(slot.bytes.data(), source, size);
        slot.bytes_valid = true;
    }
    return true;
}

} // namespace VideoCommon
