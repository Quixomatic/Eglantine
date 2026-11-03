# Changelog

All notable changes to this fork of Eglantine (Quixomatic/Eglantine) are documented here. Upstream's own
changes come in through merges of upstream `nx2` and are summarized per merge, not repeated in full.

Versions follow [Semantic Versioning](https://semver.org) (`X.Y.Z`, three segments only). Each entry is
written by the `/version-bump` skill and used as its commit message body. A `vX.Y.Z` tag on the `release`
branch builds every platform and publishes a GitHub Release (see `.github/workflows/release-binaries.yml`).

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
