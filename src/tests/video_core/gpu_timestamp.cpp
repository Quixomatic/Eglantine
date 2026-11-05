// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <chrono>

#include <catch2/catch_test_macros.hpp>

#include "video_core/gpu_timestamp.h"

namespace {

struct Timing {
    std::chrono::nanoseconds time;
    u64 gpu_ticks;

    std::chrono::nanoseconds GetGlobalTimeNs() const {
        return time;
    }

    u64 GetGPUTicks() const {
        return gpu_ticks;
    }
};

constexpr std::array Clocks{Settings::GpuClock::Normal, Settings::GpuClock::Boost,
                            Settings::GpuClock::Overclock};

} // namespace

TEST_CASE("Ampere report timestamps use driver and VI nanoseconds", "[video_core][gpu_timestamp]") {
    constexpr u64 HostTimeNs = 293'036'000'000'000;
    const Timing timing{std::chrono::nanoseconds{HostTimeNs}, HostTimeNs * 384 / 625};
    for (const auto clock : Clocks) {
        REQUIRE(Tegra::ReadGpuTimestamp(timing, true, clock) == HostTimeNs);
    }
}

TEST_CASE("Maxwell retains its tick units and clock scaling", "[video_core][gpu_timestamp]") {
    for (const u64 ticks : {u64{0}, u64{1}, u64{255}, u64{256}, u64{511}, u64{512},
                            u64{614'400'000}, u64{180'041'318'400'000}}) {
        const Timing timing{std::chrono::nanoseconds{293'036'000'000'000}, ticks};
        REQUIRE(Tegra::ReadGpuTimestamp(timing, false, Settings::GpuClock::Normal) == ticks);
        REQUIRE(Tegra::ReadGpuTimestamp(timing, false, Settings::GpuClock::Boost) == ticks / 256);
        REQUIRE(Tegra::ReadGpuTimestamp(timing, false, Settings::GpuClock::Overclock) == ticks / 512);
    }
}
