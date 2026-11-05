// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ampere/program_capture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::ProgramCaptureState;

constexpr Instruction EXIT{{0x0000794d, 0, 0x03800000, 0}};

// Lowered Maxwell words whose top bits (above `shift`) are `opcode`.
template <typename Program>
size_t CountMaxwell(const Program& lowered, u64 opcode, u32 shift) {
    return static_cast<size_t>(std::count_if(lowered.instructions.begin(),
                                             lowered.instructions.end(),
                                             [&](u64 word) { return (word >> shift) == opcode; }));
}

template <typename Program>
u64 FindMaxwell(const Program& lowered, u64 opcode, u32 shift) {
    const auto it = std::find_if(lowered.instructions.begin(), lowered.instructions.end(),
                                 [&](u64 word) { return (word >> shift) == opcode; });
    return it != lowered.instructions.end() ? *it : 0;
}

TEST_CASE("Ampere BRA.CONV always branches and BRA.DIV never does", "[video_core][ampere]") {
    // BRA.CONV UR6 / BRA.DIV ~URZ to the second EXIT: the mode is in the offset's low two bits.
    const std::array<Instruction, 3> convergent{{
        {{0x06007947, 0x10 | 3, 0x0b800000, 0}},
        EXIT,
        EXIT,
    }};
    ProgramCaptureState capture;
    REQUIRE(capture.Observe(convergent[0], 0) == ProgramCaptureState::Result::Continue);
    const auto taken = LowerToMaxwell(convergent);
    REQUIRE(static_cast<bool>(taken));
    REQUIRE(CountMaxwell(taken, 0xe24, 52) == 1);

    const std::array<Instruction, 3> divergent{{
        {{0xff007947, 0x10 | 2, 0x0b800000, 0}},
        EXIT,
        EXIT,
    }};
    const auto dropped = LowerToMaxwell(divergent);
    REQUIRE(static_cast<bool>(dropped));
    REQUIRE(CountMaxwell(dropped, 0xe24, 52) == 0);
}

TEST_CASE("Ampere TLD.SCR.LZ takes its handle from a uniform register", "[video_core][ampere]") {
    // TLD.SCR.LZ R5, R8, R4, R5, UR4, 0x0, 2D, 0x7 -> Maxwell TLD.B.
    const std::array<Instruction, 2> code{{
        {{0x04087f66, 0x30000405, 0x089e0705, 0x001f4200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 tld = FindMaxwell(lowered, 0xdd, 56);
    REQUIRE(tld != 0);
    REQUIRE(((tld >> 28) & 7) == 2); // 2D
    REQUIRE(((tld >> 31) & 0xf) == 7);
}

TEST_CASE("Ampere I2F converts signed 16-bit sources", "[video_core][ampere]") {
    // I2F.S16 R8, R8
    const std::array<Instruction, 2> code{{
        {{0x00087306, 0x00000008, 0x00101400, 0x000fe200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 i2f = FindMaxwell(lowered, 0x5cb, 52);
    REQUIRE(((i2f >> 10) & 3) == 1); // 16-bit source
    REQUIRE(((i2f >> 13) & 1) == 1); // signed
}

TEST_CASE("Ampere integer-to-float conversions take immediates", "[video_core][ampere]") {
    // I2F.U32.RP R7, 0x20; I2FP.F32.U32.RZ R4, 0x100; WARPSYNC R7
    const std::array<Instruction, 4> code{{
        {{0x00077906, 0x00000020, 0x00209000, 0x000e2200}},
        {{0x00047845, 0x00000100, 0x0020d000, 0x000fe200}},
        {{0x00007348, 0x00000007, 0x03800000, 0x000fe800}},
        EXIT,
    }};
    REQUIRE(static_cast<bool>(LowerToMaxwell(code)));
}

TEST_CASE("Ampere SUST.P keeps its channel mask", "[video_core][ampere]") {
    // SUST.P.2D.STRONG.SM.R.IGN [R6], R4, 0x0, 0x59
    const std::array<Instruction, 2> code{{
        {{0x06007b9b, 0x60005904, 0x0010a100, 0x000fe200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(((FindMaxwell(lowered, 0xeb2, 52) >> 20) & 0xf) == 1);
}

TEST_CASE("Ampere SHFL with a register lane keeps its mode", "[video_core][ampere]") {
    // SHFL.DOWN PT, R7, R0, R5, 0x1f
    const std::array<Instruction, 2> code{{
        {{0x00077589, 0x08001f05, 0x000e0000, 0x000fe200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(((FindMaxwell(lowered, 0xef1, 52) >> 30) & 3) == 2);
}

TEST_CASE("Ampere calls find a return address set ahead of them", "[video_core][ampere]") {
    // MOV R9, 0x30; MOV R4, 0x5; CALL.REL.NOINC; EXIT; RET.REL.NODEC R9
    std::array<Instruction, 5> code{{
        {{0x00097802, 0x30, 0x00000f00, 0}},
        {{0x00047802, 0x5, 0x00000f00, 0}},
        {{0x00007944, 0x10, 0x03c00000, 0}},
        EXIT,
        {{0x09007950, 0xffffffb0, 0x03c3ffff, 0}},
    }};
    REQUIRE(static_cast<bool>(LowerToMaxwell(code)));

    // The link register is written again between its MOV and the call.
    code[1] = {{0x00097802, 0x5, 0x00000f00, 0}};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(!static_cast<bool>(lowered));
    REQUIRE(lowered.error == "native call linkage mode");
}

TEST_CASE("Ampere programs go on past an EXIT that a branch jumps over", "[video_core][ampere]") {
    // A call, then a branch over an EXIT to more of the program, with another call there.
    const std::array<Instruction, 8> code{{
        {{0x00047802, 0x20, 0x00000f00, 0}},
        {{0x00007944, 0x50, 0x03c00000, 0}},
        {{0x00007947, 0x10, 0x03800000, 0}},
        EXIT,
        {{0x00047802, 0x60, 0x00000f00, 0}},
        {{0x00007944, 0x10, 0x03c00000, 0}},
        EXIT,
        {{0x04007950, 0xffffff80, 0x03c3ffff, 0}},
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.source_size == sizeof(code));
}

TEST_CASE("Ampere capture ends at the return of a helper called past the EXIT",
          "[video_core][ampere]") {
    // Ocarina 69a40579971fb867: MOV R0, 0x20; CALL.REL.NOINC; EXIT; RET.REL.NODEC R0, then the
    // next program in memory.
    const std::array<Instruction, 4> code{{
        {{0x00007802, 0x20, 0x00000f00, 0}},
        {{0x00007944, 0x10, 0x03c00000, 0}},
        EXIT,
        {{0x00007950, 0xffffffc0, 0x03c3ffff, 0}},
    }};
    ProgramCaptureState capture;
    for (u32 index = 0; index + 1 < code.size(); ++index)
        CHECK(capture.Observe(code[index], index) == ProgramCaptureState::Result::Continue);
    CHECK(capture.Observe(code.back(), 3) == ProgramCaptureState::Result::Complete);
    REQUIRE(static_cast<bool>(LowerToMaxwell(code)));

    ProgramCaptureState jump;
    CHECK(jump.Observe({{0x00007944, 0x10, 0x03c00000, 0}}, 0) ==
          ProgramCaptureState::Result::Continue);
    CHECK(jump.Observe(EXIT, 1) == ProgramCaptureState::Result::Continue);
    CHECK(jump.Observe(EXIT, 2) == ProgramCaptureState::Result::Complete);
}

TEST_CASE("Ampere uniform select and bit operations reach every uniform register",
          "[video_core][ampere]") {
    // Ocarina e8decc3e5c0c184e: @UP0 USEL UR4, UR4, 0x20, UP1. UP0 and UP1 come from R239 into
    // P6 and P5; the SEL runs under P6 and the guest predicates are restored.
    const std::array<Instruction, 2> select{{
        {{0x04040887, 0x20, 0x08800000, 0x000fe200}},
        EXIT,
    }};
    const auto selected = LowerToMaxwell(select);
    INFO(selected.error);
    REQUIRE(static_cast<bool>(selected));
    const std::array<u64, 5> expected_select{
        0x010000000207f0fcULL, 0x38e800000607fffdULL, 0x38f000000607ef00ULL,
        0x5ca002800fc6dcdcULL, 0x38f000000607fd00ULL};
    CHECK(std::search(selected.instructions.begin(), selected.instructions.end(),
                      expected_select.begin(), expected_select.end()) !=
          selected.instructions.end());

    // Ocarina 2079791ecb288b83: UBREV UR4, UR17.
    const std::array<Instruction, 2> reverse{{
        {{0x000472be, 0x11, 0x08000000, 0x000fc800}},
        EXIT,
    }};
    const auto reversed = LowerToMaxwell(reverse);
    INFO(reversed.error);
    REQUIRE(static_cast<bool>(reversed));
    CHECK(reversed.instructions.front() == 0x382801800007e9dcULL);

    // Ocarina 2079791ecb288b83: UFLO.U32.SH UR21, UR21.
    const std::array<Instruction, 2> leading{{
        {{0x001572bd, 0x15, 0x080e0400, 0x000fe400}},
        EXIT,
    }};
    const auto found = LowerToMaxwell(leading);
    INFO(found.error);
    REQUIRE(static_cast<bool>(found));
    CHECK(found.instructions.front() == 0x5c3002000ed700edULL);

    // Ocarina 2079791ecb288b83: UPOPC UR10, UR17.
    const std::array<Instruction, 2> count{{
        {{0x000a72bf, 0x11, 0x08000000, 0x000fe200}},
        EXIT,
    }};
    const auto counted = LowerToMaxwell(count);
    INFO(counted.error);
    REQUIRE(static_cast<bool>(counted));
    CHECK(counted.instructions.front() == 0x5c0800000e9700e2ULL);
}

TEST_CASE("Ampere guarded uniform comparisons keep the output when the guard is false",
          "[video_core][ampere]") {
    // Ocarina 2079791ecb288b83: @UP4 UISETP.GT.U32.AND UP0, UPT, UR21, 0x1f, UPT. UP4 and the old
    // UP0 load into P2 and P6, so a false guard merges UP0 back unchanged.
    const std::array<Instruction, 2> code{{
        {{0x1500488c, 0x1f, 0x0bf04070, 0x000fe200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    INFO(lowered.error);
    REQUIRE(static_cast<bool>(lowered));
    const std::array<u64, 6> expected{
        0x0100000001f7f0fcULL, 0x38e800000447fffdULL, 0x38f000000447ef00ULL,
        0x5b6803800fc2ed37ULL, 0x38e800000407efefULL, 0x38f000000447fd00ULL};
    CHECK(std::equal(expected.begin(), expected.end(), lowered.instructions.begin()));

    // SEL R3, R4, UR7, P0
    const std::array<Instruction, 2> select{{
        {{0x04037c07, 0x7, 0x08000000, 0x000fe400}},
        EXIT,
    }};
    const auto selected = LowerToMaxwell(select);
    INFO(selected.error);
    REQUIRE(static_cast<bool>(selected));
    CHECK(selected.instructions.front() == 0x5ca000000df70403ULL);
}

TEST_CASE("Ampere lowers the instructions Ocarina of Time rejected", "[video_core][ampere]") {
    const auto lowers = [](const Instruction& instruction) {
        const std::array<Instruction, 2> code{{instruction, EXIT}};
        const auto lowered = LowerToMaxwell(code);
        INFO(lowered.error);
        CHECK(static_cast<bool>(lowered));
    };
    // ULOP with an immediate, in four compute programs.
    lowers({{0x040e74a4, 0x00003c00, 0x0f8e020e, 0x000fc400}});
    lowers({{0x040474a4, 0x00003c00, 0x0f8e0208, 0x000fe200}});
    // RED.E.ADD F32 at a uniform address.
    lowers({{0xff00798e, 0x00000009, 0x0c10e704, 0x0021e800}});
    // SHF.R.U64 funnel, immediate.
    lowers({{0x0e0e7819, 0x00000015, 0x00001211, 0x020fe200}});
    // USHF.L.U32.HI with an immediate.
    lowers({{0x3f0d7499, 0xffffffff, 0x0801060f, 0x000fe400}});
    lowers({{0x3f057499, 0x00000001, 0x08010e04, 0x000fe400}});
    // TLD4 on a 2D array.
    lowers({{0x1e347b63, 0xb0801021, 0x001e0f32, 0x00014200}});
    lowers({{0x120a7b63, 0xb080100c, 0x001e0f06, 0x100f4a00}});
    // TLD4.PTP.F16: the upscaler's last pass, which writes the 2560x1440 image.
    lowers({{0x0a0c7b63, 0x30003000, 0x001eaf0d, 0x000fc200}});
    // UIADD3 with a carry out to UP2.
    lowers({{0x0f067290, 0x00000006, 0x0ff5e03f, 0x000fe200}});
    // TLD4.DC: the shadow pass's depth compare gather.
    lowers({{0x180c7b63, 0x30800e1c, 0x001e4f0e, 0x000f4200}});
    // LEA.HI R1, R1, -R0, RZ, 0x1e.
    lowers({{0x01017211, 0x80000000, 0x078ff0ff, 0x000fca00}});
}

TEST_CASE("Ampere depth gathers keep coordinates, reference and result pairs", "[video_core][ampere]") {
    const Instruction gather{{0x180c7b63, 0x30800e1c, 0x001e4f0e, 0x000f4200}};
    const std::array<Instruction, 2> code{{gather, EXIT}};
    const auto lowered = LowerToMaxwell(code);
    INFO(lowered.error);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.required_texture_buffer == 2);
    const u64 pred = u64{7} << 16;
    const auto copy = [pred](u64 target, u64 source) {
        return 0x5c98000000000000ULL | pred | target | (u64{15} << 39) | (source << 20);
    };
    const std::array<u64, 8> expected{
        copy(250, 24), copy(251, 25), copy(246, 28),
        0xc800000000000000ULL | pred | 250 | (u64{250} << 8) | (u64{246} << 20) | (u64{2} << 28) |
            (u64{15} << 31) | (u64{14} << 36) | (u64{1} << 50) | (u64{7} << 51),
        copy(12, 250), copy(13, 251), copy(14, 252), copy(15, 253),
    };
    REQUIRE(lowered.instructions.size() == expected.size() + 1);
    CHECK(std::equal(expected.begin(), expected.end(), lowered.instructions.begin()));
}

TEST_CASE("Ampere per-tap gathers materialize proven literal offsets", "[video_core][ampere]") {
    // Offsets in R0 and R1, an unrelated LEA.HI between them and the F16 gather (the 1080p upscaler).
    const Instruction lea{{0x08087211, 0x00000007, 0x078ff8ff, 0x000fc800}};
    const Instruction gather{{0x0a0c7b63, 0x30003000, 0x001eaf0d, 0x000fc200}};
    std::array<Instruction, 5> code{{
        {{0x00007802, 0xff00ff00, 0xf00, 0}},
        {{0x00017802, 0x01000001, 0xf00, 0}},
        lea, gather, EXIT,
    }};
    const auto move_immediate = [](u64 target, u64 value) {
        return 0x010000000007f000ULL | target | (value << 20);
    };
    const auto proven = LowerToMaxwell(code);
    INFO(proven.error);
    REQUIRE(static_cast<bool>(proven));
    const auto start = proven.source_instruction_offsets[3];
    CHECK(proven.instructions[start] == move_immediate(248, 0xff00ff00));
    CHECK(proven.instructions[start + 1] == move_immediate(249, 0x01000001));

    // The LEA.HI overwrites R1 after its literal: the offsets aren't constant and stay in Rb.
    code[2].words[0] = 0x08017211;
    const auto unproven = LowerToMaxwell(code);
    INFO(unproven.error);
    REQUIRE(static_cast<bool>(unproven));
    const auto first = unproven.source_instruction_offsets[3];
    const auto gather_word = std::find_if(
        unproven.instructions.begin() + first, unproven.instructions.end(),
        [](u64 word) { return (word >> 56) == 0xc8; });
    REQUIRE(gather_word != unproven.instructions.end());
    CHECK(((*gather_word >> 20) & 0xff) == 0);
    CHECK(((*gather_word >> 54) & 3) == 2);
}

TEST_CASE("Ampere LEA.HI negates a register base", "[video_core][ampere]") {
    const Instruction capture{{0x01017211, 0x80000000, 0x078ff0ff, 0x000fca00}};
    const std::array<Instruction, 2> code{{capture, EXIT}};
    const auto lowered = LowerToMaxwell(code);
    INFO(lowered.error);
    REQUIRE(lowered.instructions.size() == 3);
    CHECK(lowered.instructions[0] == 0x38280000002701faULL);
    CHECK(lowered.instructions[1] == 0x5c1100000007fa01ULL);
}

TEST_CASE("Ampere surface reductions keep every U32 operation", "[video_core][ampere]") {
    for (u32 op = 0; op < 8; ++op) {
        for (const u32 dimension : {1U, 3U}) {
            const Instruction instruction{{0x0400759fU, 0x00008400U | (dimension << 29),
                                           0x0010e000U | (op << 23), 0}};
            const std::array<Instruction, 2> code{{instruction, EXIT}};
            const auto lowered = LowerToMaxwell(code);
            INFO(lowered.error);
            REQUIRE(static_cast<bool>(lowered));
            CHECK(lowered.instructions[0] == (0xeb48000000000400ULL | (u64{7} << 16) | (u64{op} << 24) |
                                              (u64{dimension} << 33) | (u64{0x84} << 36)));
        }
    }
}

TEST_CASE("Ampere ISBEWR shadows a high data register", "[video_core][ampere]") {
    // A mesh stage storing an attribute from R240 must not read the uniform mirror at R240.
    const Instruction store{{0x00007927, 0x0000f000 >> 8, 0, 0}};
    const auto pressure = Shader::Ampere::PrepareRegisterPressure(store, 216);
    REQUIRE(pressure.valid);
    CHECK(pressure.instruction.Bits(32, 8) < 216);
}
} // namespace
