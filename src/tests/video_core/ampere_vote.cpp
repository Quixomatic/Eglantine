// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

TEST_CASE("Ampere uniform vote writes mapped uniform registers", "[video_core][ampere]") {
    using Shader::Ampere::Instruction;
    using Shader::Ampere::LowerToMaxwell;
    const Instruction exit{{0x0000794d, 0, 0x03800000, 0}};
    constexpr std::array mappings{
        std::pair{15U, 231U}, std::pair{16U, 232U}, std::pair{21U, 237U},
        std::pair{22U, 240U}, std::pair{27U, 245U}, std::pair{63U, 255U},
    };

    for (const auto [target, mapped] : mappings) {
        const Instruction vote{{0x00007886U | (target << 16), 0, 0x038e0100, 0}};
        const auto lowered = LowerToMaxwell(std::array{vote, exit});
        REQUIRE(static_cast<bool>(lowered));
        REQUIRE(lowered.instructions.size() == 2);
        REQUIRE((lowered.instructions[0] & 0xff) == mapped);
        REQUIRE(((lowered.instructions[0] >> 48) & 3) == 1);
    }

    // UR28 and above have no fixed host register; the program is lowered again with mirrors
    // below R216 (two for UR28, so UR28 is R214), as for the other uniform instructions.
    const Instruction high{{0x001c7886, 0, 0x038e0100, 0}};
    const auto mirrored = LowerToMaxwell(std::array{high, exit});
    REQUIRE(static_cast<bool>(mirrored));
    REQUIRE(mirrored.instructions.size() == 2);
    REQUIRE((mirrored.instructions[0] & 0xff) == 214);
}
