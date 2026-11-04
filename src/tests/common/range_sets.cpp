// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/range_sets.h"
#include "common/range_sets.inc"

namespace {
std::vector<std::pair<u64, u64>> Ranges(const Common::RangeSet<u64>& set) {
    std::vector<std::pair<u64, u64>> out;
    set.ForEach([&](u64 begin, u64 end) { out.emplace_back(begin, end); });
    return out;
}
} // namespace

TEST_CASE("RangeSet: adding a covered range changes nothing", "[common]") {
    Common::RangeSet<u64> set;
    set.Add(0x1000, 0x1000);
    set.Add(0x2000, 0x1000); // adjacent: joined
    set.Add(0x1800, 0x100);  // covered
    set.Add(0x1000, 0x2000); // exactly covered
    set.Add(0x5000, 0);      // empty
    REQUIRE(Ranges(set) == std::vector<std::pair<u64, u64>>{{0x1000, 0x3000}});
    set.Add(0x2800, 0x1000); // overlaps the end: extends
    REQUIRE(Ranges(set) == std::vector<std::pair<u64, u64>>{{0x1000, 0x3800}});
    set.Add(0x0800, 0x0800); // adjacent before: joined
    REQUIRE(Ranges(set) == std::vector<std::pair<u64, u64>>{{0x0800, 0x3800}});
}

TEST_CASE("RangeSet: subtracting outside every range changes nothing", "[common]") {
    Common::RangeSet<u64> set;
    set.Add(0x1000, 0x1000);
    set.Add(0x4000, 0x1000);
    set.Subtract(0x2000, 0x2000); // the gap between them
    set.Subtract(0x8000, 0x100);  // after both
    set.Subtract(0x0, 0x1000);    // before both
    set.Subtract(0x1500, 0);      // empty
    REQUIRE(Ranges(set) ==
            std::vector<std::pair<u64, u64>>{{0x1000, 0x2000}, {0x4000, 0x5000}});
    set.Subtract(0x1800, 0x3000); // spans the gap
    REQUIRE(Ranges(set) ==
            std::vector<std::pair<u64, u64>>{{0x1000, 0x1800}, {0x4800, 0x5000}});
    REQUIRE(set.Intersects(0x1700, 0x10));
    REQUIRE(!set.Intersects(0x1800, 0x3000));
}
