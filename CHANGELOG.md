# Changelog

All notable changes to this fork of Eglantine (Quixomatic/Eglantine) are documented here. Upstream's own
changes come in through merges of upstream `nx2` and are summarized per merge, not repeated in full.

Versions follow [Semantic Versioning](https://semver.org) (`X.Y.Z`, three segments only). Each entry is
written by the `/version-bump` skill and used as its commit message body. A `vX.Y.Z` tag on the `release`
branch builds every platform and publishes a GitHub Release (see `.github/workflows/release-binaries.yml`).

## [0.1.13] - 2026-11-04

### Internal
- `uniform_stream_reuse.h` documents why each slot keeps a CPU copy of its bytes: comparing against
  the stream allocation instead reads write-combined upload memory, which took Donkey Kong Bananza
  to 1.7 fps when tried (reverted).

## [0.1.12] - 2026-11-04

### Added
- NX2 profiler counters `same_textures` / `same_buffers`: draws whose pipeline, image views and
  samplers (or constant buffer, vertex and index bindings) match the previous draw. In Donkey Kong
  Bananza's open map 98% of draws repeat the texture side and ~3% the buffer side, which bounds what a
  "same as the previous draw" fast path in `GraphicsPipeline::Configure` can skip.

### Internal
- The open map holds 18.8 fps after the benchmark turns away and back (measured 10 s after
  recentring), the same as before turning: the higher number on screen is a smoothed counter.

## [0.1.11] - 2026-11-04

### Changed
- Small uniform buffers are compared and copied straight from guest memory when the range is
  contiguous in host memory, instead of being staged in a scratch buffer first (one pass less per
  bound uniform buffer per draw; no measurable fps change in DK, 19.5 fps on the open map).

### Internal
- Re-measured v0.1.10: the 1% low facing away is 53.8 fps, so the lower value seen once after the
  worker wake batching was noise.

## [0.1.10] - 2026-11-04

Two per-draw costs off the GPU back thread: open map 18.7 -> 19.1 fps.

### Changed
- **The Vulkan worker is woken for every few command chunks, not each one.** An idle worker was
  woken with a system call for each dispatch (one per few draws); it is now woken once 4 chunks are
  queued, and at once for submissions, present and waits for the worker. `EDEN_NX2_WAKE_BATCH=<n>`
  sets the batch (1 = the old behaviour; one run showed a lower 1% low facing away, unconfirmed).
- **Buffer LRU touches check the buffer's own last touch tick first**, skipping the LRU pool lookup
  (a cache miss per bound buffer per draw) when it was touched this frame already.

## [0.1.9] - 2026-11-04

Texture alias synchronization skips images whose aliases have not changed: Donkey Kong Bananza's
open map 17.5 -> 18.7 fps (1% low 15.5 -> 16.5), rendering unchanged.

### Changed
- **`TextureCache::SynchronizeAliases` returns at once when no alias is newer.** It walked every
  alias of an image each time a draw sampled it, comparing modification ticks, though almost none
  had changed: ~7% of the GPU back thread in DK's open areas (host_profile). Each image now keeps
  the newest modification tick among its aliases (raised when an alias is modified, added or
  synchronized), so the walk only runs when there is something to copy.

## [0.1.8] - 2026-11-04

Donkey Kong Bananza's GPU-culled draws are drawn by the host GPU straight from the records the culling
writes: the open map goes from 14.5 to 17.5 fps and the view away from it from ~42 to 60 fps
(bench_spawn, 1% lows 13.2 -> 15.5 and ~30 -> 51), rendering unchanged (baseline 0.94-0.97).

### Changed
- **GPU-written DrawIndexedIndirect records are drawn indirectly.** DK's culling pass writes a 5-word
  record per mesh (index count, instances, first index, base vertex, base instance; culled meshes get a
  count of 0) and a native macro draws each one. The split's front read every record on the CPU: a
  drain of the back thread, a flush and often a host GPU round trip, ~2,100 times a frame, so the two
  GPU threads and the host GPU took turns. The front now recognizes NVN's DrawIndexedIndirect macro by
  a dry run of the macro code (no per-game hash) and, when its record is in an entry of its own, sends
  the back a host `vkCmdDrawIndexedIndirect` reading the record from guest memory, then the macro's
  clean-up writes. Split drains go from ~31,700 to ~1,200 a second. `EDEN_NX2_INDIRECT_RECORDS=0`
  turns it off; `EDEN_NX2_INDIRECT_REPLACE=1` also replaces the base vertex/instance constants.

### Fixed
- Indirect draws mark their argument buffer as used, so a later upload to it in the same batch is not
  reordered ahead of the draw (`BindHostDrawIndirectBuffers`).
- The NX2 profiler's counters were stored in a fixed array of 10; it is now sized by the counter enum.

### Added
- Profiler counters for GPU-written command list reads (`list_copied`, `list_synced`, `list_no_page`,
  `list_draw_written`, `list_stale`): why a list takes the slow drain-and-flush path.
- Diagnostics: `EDEN_NX2_PBFLUSH_LOG` (which macro calls take parameters from GPU-written pages, and how
  often the GPU really had newer words) and `EDEN_NX2_MACRO_TRACE=<method>` (parameters and register
  writes of a macro's first calls).

### Internal
- Found with the MCP: `perf_sample`/`gpu_profile` (draw throughput pinned at ~110k/s, the front waiting
  ~75%), the new `host_profile` (which drains and host waits, by call stack), `compare_runs` and
  bisect switches. The black ground of the first version came from the back's method sink: state
  writes wait there until an executable method arrives, and the indirect draw has to apply them first
  (`ConsumeSink`).

## [0.1.7] - 2026-11-04

A sampling profiler for the emulator's own threads, from the MCP, and fewer GPU-split drains for
macro parameters on CPU-written pages.

### Added
- **MCP `host_profile`**: samples named host threads (GPU, GPUBack, CPUCore_*...) about 1000 times
  a second and returns their hottest functions, callers and inclusive stacks, symbolized from the
  PDB (Windows). eden-cli's `EDEN_NX2_SAMPLE` sampler moved to `common/host_sampler` and now backs
  both.

### Changed
- **Macro parameters continued in their own entry drain the GPU split only when their pages are
  GPU-written** (checked again after the queued compute and copy work ran, as for GPU-written
  command entries). Games whose parameters are CPU-written no longer drain for each one. Donkey
  Kong Bananza's are on GPU-written pages, so it is unchanged there (still about 15,000 drains a
  second, now listed as `pbflush` instead of `other`); rendering checked against a baseline.

## [0.1.6] - 2026-11-04

Fixes an intermittent crash in Donkey Kong Bananza that came in with upstream's batched fence
submissions, and documents every MCP tool.

### Fixed
- **Crash recording Vulkan commands with the GPU thread split** (access violation in
  `Scheduler::Record`, about 2 of 3 benchmark runs). The back submitted deferred work from its idle
  hook after it had told the front it was caught up; a front that drained to that point was recording
  fence-release commands itself, and the deferred flush swapped the command chunk under it. The back
  now submits deferred work at the end of each batch, before it publishes its position
  (`gpu_split.cpp`). Six benchmark rounds and three stops ran clean; open map 14.4-14.7 fps.
- MCP: `input_sequence` no longer says the TAS tools are "coming", and the server instructions say
  that booting, pausing, stopping and quitting need only the control lease (writes are for input,
  memory, settings and breakpoints).

### Added
- `docs/MCP-tools.md`: reference for all 98 MCP tools (parameters, gating, examples), linked from the
  other MCP docs; the user handbook, command line page and `tools/mcp/README.md` corrected where they
  disagreed with the code (Allow Writes scope, lease markers, prompts, discovery file contents).

## [0.1.5] - 2026-11-03

Fixes the Linux release builds, which the v0.1.2 tag run failed.

### Fixed
- An unused variable in the partial texture re-upload (`texture_cache/util.cpp`) failed GCC's
  `-Werror=unused-but-set-variable` in both Linux GCC builds.

## [0.1.4] - 2026-11-03

Brings in upstream `nx2` 8615d8d53e (210 commits). Donkey Kong Bananza still renders correctly and
replays to_gameplay (65 s from boot); bench_spawn 12.6 / 39.1 fps, the same as 0.1.2.

### Added
- **Upstream's work since the fork**, among them: game DLSS network emulation with health checks and a
  replacement upscale (Fortune's Weave), native geometry shaders, more Ampere instruction forms
  (LEA.HI.X, MUFU, sub-word local spills, F2I |Rs|, guarded ULEA/UIADD3), SUST with partial channel
  masks, storage-buffer tracking that stops at loaded pointers, zero-copy chunked texture uploads, a guard
  against garbage draw counts, the NUP metadata editor, the Hyrule Warriors and Fast Fusion fixes.

### Changed
- **HMMA outside a recognized game DLSS evaluation is emulated again.** Upstream emulates matrix
  multiplies only inside the evaluations it recognizes (Fortune's Weave's); DK Bananza's upscaler is not
  one, so its passes were rejected and the scene went purple. Inside a recognized evaluation upstream's
  checks still decide. `EDEN_NX2_TENSOR=off` rejects HMMA again.

### Fixed
- Merge duplicates both sides had added (texture `UploadInChunks`, geometry OUT/LEPC/0x357, a G8R24
  format mapping): upstream's versions kept.
- Tests: the global-address test passes an environment; the NUP metadata test closes the archive before
  editing it again (Windows can't replace an open file); the vote and F2I tests expect UR28 mirrors and
  the native I8 conversion.

### Internal
- Merge choices, per hunk: upstream's lowering API, HMMA (f32, borrowed registers), LEA.HI.X, MUFU,
  I2F immediates; ours where it covers more (guarded uniform ops with the post-emission check, UR28+
  mirrors for VOTEU, unsigned and signed I8 F2I, 16-bit I2F from uniform registers, TLDS .F16, ignored
  spill cache hints); both for the svc trace hook, pipeline stats with the mesh check, draw counters with
  the draw guard, the descriptor-buffer texel path with the invalid-format guard, pruned searches with the
  memory-load stop, MKW's fps patch (upstream's) with the DK 30 fps lock.
- Known issue: one crash (access violation recording `QuadIndexBuffer::BindBuffer`) during a bench_spawn
  run; not reproduced on the second run.

## [0.1.3] - 2026-11-03

The main window's title shows the build again instead of PokeDinner's fixed string.

### Changed
- **Window title**: back to upstream's `Eglantine | <version> | <compiler>` (which identifies the exact
  commit and branch), with `| Quixomatic fork` added at the end.

## [0.1.2] - 2026-11-03

Brings in the PokeDinner fork (Franseven/PokeDinner-a-Eglantine-Fork `master` 216b5034c6, 171 commits).
Donkey Kong Bananza's ground renders solid again, and the to_gameplay route replays end to end.

### Added
- **PokeDinner's fixes and features**, among them: sparse terrain arrays bound layer by layer with memory
  only where the GPU writes (DK's see-through ground), AMD sparse-region and RDNA fixes, Switch 2 ASTC
  limits and partial re-uploads, texture-cache format mappings (X8Z24, G8R24), TXQ from any descriptor
  buffer, NVN mapping modify, shader-reuse and pipeline work, the DK Bananza 30 fps lock checkbox, Mario
  Kart World 30 fps / dynamic fps modes and a remembered recommended-settings prompt.

### Changed
- **HMMA uses PokeDinner's f32 lowering** (equivalent to 0.1.1's, plus a predicate check).
  `EDEN_NX2_TENSOR` wins, otherwise the caller's mode, and unset still emulates.

### Fixed
- Build with warnings as errors: a local in the HMMA lowering shadowed a parameter (MSVC C4457), and the
  AArch32 fatal-context copy tripped GCC's `-Wclass-memaccess` (it broke the Linux release builds).
- Removed PokeDinner's stale HMMA test that still expected the old f16 HFMA2 chain.

### Internal
- Merge conflicts: HMMA and the F2I / guarded-uniform tails of `lower_to_maxwell.h` (both sides'
  conversion modes and checks kept), the MKW prompt (PokeDinner's), `-d`/`-i` parsing and the video
  decoder EOS handling (dev's).
- Verified on DK: route `to_gameplay` completes (72 s from boot), `bench_spawn` 13.0 / 39.0 fps.

## [0.1.1] - 2026-11-03

Ports the private fork's own fixes: Donkey Kong Bananza no longer renders purple, and the NX2 GPU
profiler says why the split GPU threads wait.

### Fixed
- **HMMA (tensor) is emulated by default with f32 accumulation.** Each product is summed in f32 and
  rounded to f16 once per K=8 block, as the tensor cores do; the old f16 HFMA2 chain overflowed to
  inf/NaN in DK Bananza's upscaler (purple screen). HMMA.1688 is handled too. `EDEN_NX2_TENSOR=stub`
  keeps the diagnostic D = C and `EDEN_NX2_TENSOR=off` rejects HMMA
  (`shader_recompiler/frontend/ampere/lower_to_maxwell.h`, test in `ampere_returns.cpp`).

### Added
- **Split drains by site and draw counters in the NX2 profiler.** `EDEN_NX2_GPU_PROFILE` and the MCP's
  `gpu_profile` / `perf_sample` report why the front waited for the back (command sync, GPU-written
  params, front init, MME DMA read, puller, ring full, writer wait) and draws / empty draws per second.

## [0.1.0] - 2026-11-03

The first release of this fork: upstream Eglantine `nx2` (3779433f61) with the built-in MCP server, and a
release pipeline that builds from semver tags.

### Added
- **Built-in MCP server** (Model Context Protocol) so AI agents and scripts can drive and inspect the
  emulator: boot and stop games, screenshots, input and frame-exact TAS, an event timeline of logs, crashes
  and unimplemented service commands with backtraces, guest memory read/search/watch/scan, threads and
  backtraces, IPC/SVC tracing, settings, GPU-thread profiling, recorded routes with screen fingerprints,
  benchmarks and visual baselines. Off by default; loopback only; Allow Writes and an optional token gate
  state changes. Code in `src/mcp/`, docs in `docs/user/MCP.md`, `docs/MCP.md` and `tools/mcp/README.md`.
- **The emulator's debugging features from the MCP**: breakpoints and watchpoints through the GDB stub
  (hits as timeline events with registers and backtraces), shader/macro dumps, crash reports, the FS
  access log, and `ab_test` to measure what a setting changes.
- **Configure > Debug > MCP** tab for the server's settings; Tools > MCP Server menu; a status-bar label; a
  read-only browser dashboard; `eden-cli --mcp` for headless runs.

### Changed
- **Releases build from semver tags.** `release-binaries.yml` runs on `vX.Y.Z` (and `vX.Y.Z-label`
  prerelease) tags whose commit is on `release`, names the release after the tag and uses
  `.release/patch-notes/vX.Y.Z.md` or this changelog's section as its body. Pushes to `nx2` no longer
  build. The Discord ping is skipped without its webhook secret.

### Fixed
- eden-cli: `-d`/`--debug` and `-i`/`--input-profile` no longer crash on launch; TAS advances in eden-cli.
- Switch 2 module names are found, so backtraces are symbolized.
