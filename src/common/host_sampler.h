// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace Common::HostSampler {

/// Sample counts per function for one host thread, most frequent first.
struct ThreadReport {
    std::string name;
    unsigned samples{};
    /// Where the thread is (the innermost frame).
    std::vector<std::pair<std::string, unsigned>> self;
    /// Innermost frame and its caller ("a  <-  b").
    std::vector<std::pair<std::string, unsigned>> self_caller;
    /// Every function on the stack, counted once per sample.
    std::vector<std::pair<std::string, unsigned>> inclusive;
};

struct Result {
    std::vector<ThreadReport> threads;
    /// Every distinct stack with its count, root first ("thread;a;b;c count" per line).
    std::string folded;
    /// Why nothing was sampled (no matching thread, not supported on this platform).
    std::string error;
};

/// A sampling profiler for named host threads (Windows only). Thread names match exactly, or by
/// prefix with a trailing '*' (CPUCore_*). For `seconds`, each thread is paused about 1000 times
/// a second and its call stack unwound and symbolized (dbghelp, from the PDB next to the
/// executable). Blocks for the duration; each list keeps its `top` entries.
Result Sample(const std::vector<std::string>& patterns, double seconds, size_t top);

/// EDEN_NX2_SAMPLE=<thread names, comma separated>: runs Sample on a thread from
/// EDEN_NX2_SAMPLE_START seconds (default 60) for EDEN_NX2_SAMPLE_SECONDS (default 30) and writes
/// the report to EDEN_NX2_SAMPLE_OUT (default nx2_samples.txt) and the stacks to <out>.folded.
void StartFromEnvironment();

} // namespace Common::HostSampler
