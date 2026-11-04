# Changelog

All notable changes to this fork of Eglantine (Quixomatic/Eglantine) are documented here. Upstream's own
changes come in through merges of upstream `nx2` and are summarized per merge, not repeated in full.

Versions follow [Semantic Versioning](https://semver.org) (`X.Y.Z`, three segments only). Each entry is
written by the `/version-bump` skill and used as its commit message body. A `vX.Y.Z` tag on the `release`
branch builds every platform and publishes a GitHub Release (see `.github/workflows/release-binaries.yml`).

## [0.1.22] - 2026-11-04

Donkey Kong Bananza's indirect draw record resets are written to the host buffers in one batch:
open map 24.5 -> 25.0 fps (same build, switch off and on; 1% low 21.9 -> 22.2), rendering unchanged.

### Changed
- **Indirect record resets are batched** (`RasterizerVulkan::TryNx2IndirectReset`). Each reset
  writes `{0, 1, 0, 0, 0}` to every record of a list; each record was an inline write of its own
  (buffer cache lock, buffer lookup, staging allocation, copy command, and a hole carved in the
  GPU-modified range set that the culling pass's next write filled again). Guest memory is now
  written per record (skipped when it already holds the bytes) and `BufferCache::InlineRecords`
  writes every GPU-written record under one lock from one 20-byte staging copy, with one
  multi-region copy per buffer; records the GPU had not written are invalidated as CPU writes,
  as before. The GPU back thread's `draw` zone went from 90 to 71 ms/s.
  `EDEN_NX2_RESET_BATCH=0` writes each record as an inline write again.

## [0.1.21] - 2026-11-04

Draws that hit the texture binding cache push their image descriptors from the cache: Donkey Kong
Bananza's open map 1% low 21.7 -> 22.4 fps (same build, switch off and on), rendering unchanged.

### Changed
- **The binding cache also keeps the descriptor entries its views and samplers produced** (image
  view and sampler handles, and whether each texture is rescaled) and pushes them again on a hit,
  skipping the per-texture view, sampler and rescaling lookups of `PushImageDescriptors`. The
  entries change only with what already invalidates the cache (image deletion, rescaling).
  `EDEN_NX2_BINDCACHE_ENTRIES=0` pushes them the full way.

## [0.1.20] - 2026-11-04

Inline writes stop taking the query cache's lock for pages without queries: Donkey Kong Bananza's
open map 24.0 -> 24.4 fps (1% low 21.2 -> 22.1), rendering unchanged.

### Changed
- **The query cache keeps a bit filter of the device pages that ever held a cached query** and
  skips its mutex and page lookups for ranges with none (`QueryCacheBase::IterateCache`). Every
  inline write invalidates queries in its range, and DK's indirect record resets made tens of
  thousands of them a second, each locking `cache_mutex` (~1.5% of the GPU back thread) to find
  nothing. The filter never clears; a stale bit only takes the old path.

## [0.1.19] - 2026-11-04

Range sets skip work that changes nothing: Donkey Kong Bananza's open map 23.5 -> 24.0 fps (1% low
20.6 -> 21.2), rendering unchanged.

### Changed
- **`Common::RangeSet::Add` returns at once when the range is already covered**, and `Subtract` when
  it overlaps nothing: one lookup instead of Boost's insert-and-merge or erase walk. The buffer cache
  re-adds the same GPU-written ranges for every draw (`MarkWrittenBuffer`: `RangeSet::Add` was 2.7%
  of the GPU back thread) and subtracts written ranges from download sets that rarely hold them.

### Added
- Tests for both early returns (`src/tests/common/range_sets.cpp`).

## [0.1.18] - 2026-11-04

Buffer binding redoes less per draw: Donkey Kong Bananza's open map 22.9 -> 23.5 fps with the
same build's switch off and on (1% low 20.3 -> 20.6), rendering unchanged.

### Changed
- **Vertex and index buffer binds skip synchronization while nothing changed.** Every bind walked
  the memory tracker's page bits for CPU-modified data, ~1.3 million times a second in DK, almost
  always finding none. Each tracker region (4 MiB) now carries an epoch that changes whenever any
  of its pages may become CPU-modified; a bind whose range was synchronized at the current epoch
  (same buffer, no buffer registered or removed since) skips the walk. `update buffers` 152 -> 130
  ms/s on the GPU back thread. `EDEN_NX2_SYNC_MEMO=0` synchronizes every bind again.
- Graphics storage buffer bindings keep `GetMemoryLayoutSize` results (a lock and a map lookup
  each) until the GPU address space is remapped, and translate the address once instead of twice.
- Native 64 KiB uniform banks are looked up in the per-frame set once per slot and frame, and small
  uniform buffers inside one page take one host pointer lookup instead of two.

### Added
- Profiler counter `sync_skipped`.

## [0.1.17] - 2026-11-04

The texture binding cache works and is on by default, and writes that repeat the bytes guest
memory already holds no longer invalidate every cache: Donkey Kong Bananza's open map 22.0 -> 22.8
fps (1% low 19.9 -> 20.8, ~148k -> ~157k draws a second), rendering unchanged.

### Fixed
- **The binding cache's hit path ran two failing `ASSERT`s per draw.** It checked the view and
  sampler counts before filling them from the cache, so every hit logged two critical lines
  (~77,000 a second): that was the whole 6.1 fps of v0.1.16. The counts are checked after the fill.

### Changed
- **Texture binding cache on by default** (`EDEN_NX2_BINDCACHE=0` turns it off). In DK 92% of
  draws reuse their image views and samplers; `read handles` 56 -> 36 and `fill image views`
  46 -> 29 ms/s on the GPU back thread, 0 mismatches in verify mode.
- **Inline and constant buffer data writes of unchanged bytes skip cache invalidation.** DK resets
  its indirect draw records and rewrites its constants with the same values every frame: 64% of
  inline writes (~105k a second) and 63% of constant buffer data writes (~437k a second) changed
  nothing. They still drop pending query results; writes onto buffer data the GPU may have written
  (uploaded in place, or on a page the buffer cache ever marked GPU-written) invalidate as before.
  Ranges invalidated per second 383k -> 182k. `EDEN_NX2_INLINE_SAME=0` turns it off.

### Added
- Profiler counters `inval_ranges` / `inval_bytes` (ranges invalidated in every cache),
  `inline_deferred` / `inline_same`, `cb_same` / `cb_written`, `sync_uploads` /
  `sync_upload_bytes` (buffer synchronizations that uploaded CPU-modified data).

## [0.1.16] - 2026-11-04

A value-based binding cache for textures in `GraphicsPipeline::Configure`, off by default while its
hit path is investigated.

### Added
- **Texture binding cache** (`EDEN_NX2_BINDCACHE=1`, off by default): a pipeline remembers its last
  image views and samplers with the values they came from (each handle word and its 32-byte TIC and
  TSC entries, with host pointers to them) and reuses them while those compare equal, skipping the
  address translations and lookups of a full resolution. `EDEN_NX2_BINDCACHE_VERIFY=1` resolves
  every hit the full way too and logs differences. In DK: 92% hits and no mismatches in verify
  mode, but with the cache on alone the open map fell to 6.1 fps (`configure` 713 ms/s), a cost in
  the hit path that is not understood yet.
- Map serials: `Tegra::g_gpu_map_serial` (GPU virtual memory map/unmap) and
  `Core::g_device_map_serial` (device memory map/unmap) invalidate kept host pointers;
  `TextureCache::binding_cache_serial` changes on descriptor table rebinds and image deletion.
- Profiler counters `bindcache_hits` / `bindcache_verified`.

### Internal
- Tried and reverted: a write-generation design (pages armed with write protection, generations
  bumped by every write source). DK rewrites its handle constant buffers and descriptor pools every
  frame with the same values, so armed pages were disarmed constantly (8 fps from protection churn;
  18% hits once volatile pages were skipped). Values, not writes, are what repeat.

## [0.1.15] - 2026-11-04

The GPU back thread submits every 512 draws instead of every 128 on Switch 2 titles: Donkey Kong
Bananza's open map 21.4 -> 22.0 fps, 1% low 16.3 -> 19.9.

### Changed
- **`EDEN_NX2_FLUSH_DRAWS` defaults to 512** (was 128). At ~150k draws a second each submission
  cost ~55 us of the back thread (`flush work` 65 ms/s); 512 and 1024 measured the same. The
  variable still overrides it.

### Internal
- Measured and dropped: forcing small uniform buffers onto the cached path (bound in place,
  uploaded when written): 15.5 fps, because DK writes that memory constantly.

## [0.1.14] - 2026-11-04

Macro register writes reach the GPU back thread in batches: Donkey Kong Bananza's open map ~19 ->
21.4 fps (the back now prepares ~146k draws a second instead of ~128k), rendering unchanged.

### Changed
- **Consecutive macro writes to one engine travel as one `Run` entry** in the GPU split's ring
  instead of one entry each. The back loops over them with one method-sink check and one profiler
  scope, skipping the per-entry dispatch (~2.5-3 million writes a second in DK): the back's method
  zone went from 175 to 124 ms/s and its unattributed time from 138 to 67 ms/s. A run stays open
  only until something else is written or the ring is published, so the order is unchanged.

### Added
- Profiler counters `uniform_reused` / `uniform_copied`: small uniform binds that rebound the
  previous allocation versus copied new bytes (DK: ~76% reused, ~6 uniform binds per draw).

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
