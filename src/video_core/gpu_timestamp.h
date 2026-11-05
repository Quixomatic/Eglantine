// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/common_types.h"
#include "common/settings_enums.h"

namespace Tegra {

/// How much faster Boost and Overclock make Switch 2 GPU work look (Lichii #114).
[[nodiscard]] constexpr u64 AmpereClockDivisor(Settings::GpuClock clock) {
    switch (clock) {
    case Settings::GpuClock::Boost:
        return 4;
    case Settings::GpuClock::Overclock:
        return 16;
    default:
        return 1;
    }
}

/// The timestamp a query report writes. Switch 2 titles expect nanoseconds, the unit of the other
/// clocks they read, and calibrate CPU time against it. Boost and Overclock shrink only the time
/// since the last present (present_ns): GPU durations within a frame look shorter, while a
/// timestamp never runs ahead of the CPU clock and trails it by less than the time since that
/// present. Switch 1 titles get scaled GPU ticks.
template <typename Timing>
[[nodiscard]] u64 ReadGpuTimestamp(const Timing& timing, bool ampere, Settings::GpuClock clock,
                                   u64 present_ns) {
    if (ampere) {
        const u64 now = static_cast<u64>(timing.GetGlobalTimeNs().count());
        if (now <= present_ns) {
            return now;
        }
        return present_ns + (now - present_ns) / AmpereClockDivisor(clock);
    }
    const u64 ticks = timing.GetGPUTicks();
    switch (clock) {
    case Settings::GpuClock::Boost:
        return ticks / 256;
    case Settings::GpuClock::Overclock:
        return ticks / 512;
    default:
        return ticks;
    }
}

} // namespace Tegra
