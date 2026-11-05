# Changelog

All notable changes to this fork of Eglantine (Quixomatic/Eglantine) are documented here. Upstream's own
changes come in through merges of upstream `nx2` and are summarized per merge, not repeated in full.

Versions follow [Semantic Versioning](https://semver.org) (`X.Y.Z`, three segments only). Each entry is
written by the `/version-bump` skill and used as its commit message body. A `vX.Y.Z` tag on the `release`
branch builds every platform and publishes a GitHub Release (see `.github/workflows/release-binaries.yml`).

## [0.1.38] - 2026-11-05

Ocarina of Time's foliage no longer flashes white while the camera moves, at Low GPU accuracy with the
GPU thread split on, and the forest renders faster. Confirmed by the user in play. One sample in the
forest read 23.1 fps (1% low 20.7) and 63,000 draws a second, against 20.6 (17.5) and 31,000 before,
at a similar but not identical spot.

### Fixed
- **Guest CPU reads of GPU results were stale** (Lichii #147, still open there; its commit 23d33887a8).
  Read pages of Switch 2 titles were marked preflushable after their first read, then only refreshed
  at fences, so the game could read older GPU output: in OOT, occlusion results that hid the foliage
  for a frame. These titles now keep read traps and flush on demand. A read that must flush downloads
  every range the CPU has read back after GPU writes in one batch, with one GPU wait. Consumed traps
  re-arm on the next GPU write. `EDEN_NX2_PREFLUSH=1` restores preflushing, `=0` forces flushing on
  demand.
- `DownloadMemory` clips each buffer's download to the requested range.

### Changed
- **GPU writes copy back only their preflushable pages at fences** (Lichii #104), instead of the whole
  write. Our copy-back of CPU-read pages (Pokopia's sky) is kept. `EDEN_NX2_EXACT_PREFLUSH=0` turns it
  off.
- **`MaxContinuousRange` answers are cached per thread until the GPU page table changes** (Lichii #146).
  Native storage buffer binds walked up to 8 MiB of pages each: `read handles` on the GPU back thread
  299 -> 88 ms/s in that sample.
- **The running occlusion query is capped at one bank across submissions** (Lichii #121's second
  commit). OOT's title screen and file select count without resetting or reporting, so the query had
  grown to 16,384 slots by the start of gameplay.

### Internal
- `ReadTrapTracker` (`buffer_cache/read_trap_tracker.h`) and Lichii's 4 readback tests. All 211 test
  cases pass. Donkey Kong Bananza was not measured.
- Known: 4 compute programs are still rejected on `ULDC` indexed byte and 16-bit loads.
- The flash investigation found that GPU accuracy High or `gpu_fence_behavior=Balanced` also hid it,
  which pointed at results reaching the game late. Its temporary diagnostics are not committed.

## [0.1.37] - 2026-11-05

Ocarina of Time's grass and foliage render, and its 3D stays at full resolution. Foliage still flashes
white while the player moves and for about 15-20 seconds after stopping. A sample with these changes
reads about 21 fps (1% low about 17) where v0.1.36 read about 39 at another spot, so the two are not
directly comparable. Donkey Kong Bananza was not measured.

### Added
- **Storage buffers behind dynamic and loaded pointers** (Lichii `d854ff0de`, `f9acc60a5`, on its test
  branches). Native programs bind every mapped slot of a pointer array read at a computed offset, and a
  buffer whose pointer is loaded from a table in memory
  (`StorageBufferDescriptor::indirect_offset`). The address walk follows `ShuffleIndex`, phis and selects
  of table pointers, and table entries of compute programs use only free bindings. Table entries
  are classified by `IsCbufIndirectStorageBufferPointer`, which `RecordingEnvironment` forwards and records.
- **Pointer table pools bind to the end of their mapping, up to 64 MiB, when read-only** (Lichii
  `4dcbd1cb9`), so grass instance records past 8 MiB read correctly.
- **Built-in "Max Resolution" patch for OOT** (Lichii #124, still open there). It takes the game's own
  branch for dynamic resolution disabled, checked against title, module, build id and the original
  word, and a user mod at that word wins. It's listed in Add-Ons, where it can be turned off.

### Changed
- **OOT's graphics storage buffers are sized from their mappings** (from Lichii `37569c01d`, which does
  this for every native game). `EDEN_NX2_OOT_MAPPED_SSBO=0` turns it off with the compute sizing.
- **Shader cache version 24**: every game rebuilds its shader cache once.

### Internal
- `.plans/dk-perf-scripts/flash_oot.sh` counts one-capture white flashes from a screenshot burst.
- Known: `read handles` takes about 300 ms/s of the GPU back thread in OOT's forest, and 4 compute
  programs are rejected on `ULDC` indexed byte and 16-bit loads. All 207 tests pass. Lichii's tests for
  the built-in patch and pointer forms are not ported.

## [0.1.36] - 2026-11-05

Switch 2 query report timestamps are in nanoseconds, the unit of the other clocks these games read
(Lichii #84, which took Star Fox from about 31 to 60 fps). Ocarina of Time's open area is unchanged on
average: 39.3 fps against 39.6 with it off, 1% low 28.1 against 30.7 (one run each).

### Fixed
- **`GPU::GetTicks` returned scaled GPU ticks to Switch 2 titles** (`gpu_timestamp.h`). Switch 1 titles
  keep their ticks and the Boost and Overclock scaling. On Switch 2 titles, Boost and Overclock no longer
  make GPU durations look shorter. `EDEN_NX2_NS_TIMESTAMPS=0` returns to the ticks.

### Internal
- `.plans/dk-perf-scripts/measure_oot.sh`: boots OOT, runs its `to_gameplay` route and takes a 20 s
  sample with a GPU profile and a screenshot.
- Tests from Lichii's `gpu_timestamp` cases (207 pass, run before the switch was added).

## [0.1.35] - 2026-11-05

More of Lichii's Ocarina of Time fixes, hand ported. Tested in the game: it looks better and a session
log shows no rejected shaders, but falling leaves still flash white. Our bloom port was also compared
with Lichii's final PR #87 and needs no change.

### Fixed
- **White flashes from occlusion counts** (`Scheduler::EndPendingOperations`, Lichii PR #121, still open
  there). Each submission reset the counter, so a report after one could read too low. It now pauses it.
- **ISBEWR read a uniform mirror instead of its data register** when that register is high, which gave
  Link's house door a NaN that bloom turned into a white glow (Lichii #100).
- **F2I with a constant buffer source dropped its `|x|` and `-x`** (Lichii `1ad14bebb`).

### Added
- **More gathers** (Lichii #112): depth compare (`TLD4.DC`), and per-tap offsets found across unrelated
  instructions and outgoing branches, then written to scratch registers as literals. If the offsets can't
  be proven they stay in Rb, where Lichii rejects the program.
- `LEA.HI` with a negated register, and `SURED` on buffers with every U32 operation (Lichii #112).
- **Multiplies by 0 and 1 fold**, and so does the halves recombine an XMAD by 1 leaves, so OOT's sky
  ambient pass finds its storage buffer (Lichii `3248b7cb1`, not merged there yet).

### Changed
- **Shader cache version 23**: every game rebuilds its shader cache once.

### Internal
- Tests from Lichii's encodings. All 205 cases pass. Donkey Kong Bananza was not measured.

## [0.1.34] - 2026-11-05

Ocarina of Time reaches gameplay with its scene and minimap showing, instead of a black screen with
only the HUD, and no longer crashes after its first cutscene. Its bloom and fog are still wrong. The
bloom work is from Lichii's PR #87 as it stood before it was finished, and newer Lichii fixes are
still to come. Donkey Kong Bananza was not measured on this version.

### Fixed
- **Shader reuse hid native programs from translation** (`vk_shader_reuse`). `RecordingEnvironment`
  did not forward `IsNativeProgram()` or `IsCbufStorageBufferPointer()` and did not copy the mesh
  registers, so with reuse on, native programs were translated as non-native with zeroed mesh
  registers: register shadow copies were dropped, storage buffers failed to track, and every mesh
  pipeline failed with `Mesh output topology 0`. This affected other native games too.
- **Crash from preparing the null image view** (`PrepareImageView`, present since 0.1.16). The binding
  cache's hit path prepared null views, which ran on null image slot 0: a 0-byte staging request or
  `Touch(-1)`. It can crash any game and is in the 0.1.32 and 0.1.33 releases.
- **The black screen.** `TLD4.PTP.F16` was rejected because its rewrite moves destinations to scratch
  registers that the per-tap check refused. That was the upscaler's last compute pass, so the final
  image held only the HUD.
- **`CALL.REL.NOINC` is captured as a call** (`program_capture.h`), `SHF.R.U64` no longer clamps its
  shift, and an occlusion query no longer starts on a deferred clear, which began it outside the render
  pass.

### Added
- **Hand port of Lichii's OOT shader work** (PR #87 up to 4435506c96, plus 325e0a867b):
  - The full register shadow list, and opcodes `0x4a4`, `0x243`, `0x499` and `0xc07`.
  - F32 and signed reductions, signed REDUX, 64-bit shifts and negated F2I.
  - UBREV, UFLO and UPOPC on every uniform register, guarded UISETP and USEL, and ISETP.EX with
    accumulation.
  - Array gathers, UIADD3 carry forms, and global atomic add and umax in SPIR-V.
- **Lichii's pointer tracker in the global memory pass** (`global_memory_to_storage_buffer_pass.cpp`),
  alongside our pruned search. Its `coherent` flag on loads is left out.
- `KeplerCompute::SizesStorageBuffersFromMapping()`: OOT's native dispatches size storage buffers from
  their mappings, as game DLSS passes do. `EDEN_NX2_OOT_MAPPED_SSBO=0` turns it off for OOT.
- `EDEN_NX2_METHOD_RUN=0` turns `CallMethodRun` off, for bisecting.

### Changed
- **Shader cache version 22.** Caches written with the shader reuse bug have no storage pointer
  classifications, so their pipelines failed at boot and stayed null for the session. Every game
  rebuilds its shader cache once.
- The DLSS chain only starts in Fortune's Weave, as in Lichii.

### Internal
- Tests: Lichii's exact-encoding tests and the exact words from OOT's rejections
  (`ampere_lowering.cpp`).
- Known: performance in OOT is uninvestigated (game speed dropped to 29% during shader builds, and one
  compute pipeline took 18 s in the driver). Lichii's `fix/oot-boot` rejects the same upscaler pass and
  still shows a picture, which is unexplained.

## [0.1.33] - 2026-11-04

Brings in upstream `nx2` e736dfde9e (57 commits), mainly for OOT, which now gets past its startup
abort. Donkey Kong Bananza's open map holds 25.3 fps (1% low 20.8 against 22.9 before the merge, one
run each; facing away 60 fps, 1% low 51.0 against 55.2), rendering unchanged (baseline 0.959).

### Added
- **Upstream's work since the last merge**, among them: `WaitForAddress` arbitration type 3
  (`WaitIfEqual64`, the OOT fix), Phi nodes of more types in the SPIR-V backend, the H0_NH1 half
  source fix, the NUP update's display version in the game list tooltip and NUP fixes, updated shared
  font data, the Polish language code, the subgroup size check for compute pipelines, and conditional
  barriers on Intel's ANV driver.

### Changed
- **Upstream removed the "Use the game's own DLSS" setting.** The game's network runs where it can,
  and `EDEN_NX2_DLSS_REPLACEMENT` still asks for the replacement.
- HMMA emulation stays on by default outside recognized game DLSS evaluations, where upstream limits
  it to Donkey Kong Bananza.

### Not taken from upstream
- **Donkey Kong Bananza's wait for every frame at present (#506).** It is meant to stop freezes and
  rubberbanding, but it took the open map from 25.2 to 19.0 fps and the view away from it from 60 to
  37. `EDEN_NX2_FRAMES_AHEAD=0` turns it on.
- **Upstream's new CI pipeline** (`ci.yml` and the workflows, scripts and `VERSION` file that go with
  it). `dev` is this fork's default branch, so its daily schedule would run here and publish
  pre-releases. `release-binaries.yml` stays.
- **Mapped-size storage buffers on every native compute dispatch (#535).** Only game DLSS passes use
  them, as upstream's #561 did before it was reverted (#569; #571 is open to restore it): it measured
  Hyrule Warriors falling from 42-57 fps to about 2, then hanging.

### Internal
- Merge choices, per file: both sides in `nx2_gsv.h` (`mkw_smooth` and upstream's `ran_ahead` stat)
  and `StorageBufferBinding` (`layout_size_memo` and the native pointer sizing); upstream's UISETP
  and global address test, which match ours in logic; our `CHANGELOG.md`.
- Tests pass (196 cases, run before the frame wait was taken out, which changed only `nx2_gsv.h`).

## [0.1.32] - 2026-11-04

The release workflow no longer loses a whole release to one warning or one stalled download.

### Fixed
- **Release builds no longer treat compiler warnings as errors** (`-DENABLE_WERROR=OFF`). A warning
  failed both Linux GCC builds of the v0.1.0 and v0.1.2 tags.
- **The MSYS2 setup retries up to twice.** A stalled package mirror failed the Windows PGO build of the
  v0.1.2 tag.
- **A failed platform no longer holds back the others.** The release used to publish only if every
  build succeeded, so neither tag produced one. It now publishes what built and its notes list what is
  missing.
- `memory_manager.cpp` includes what the same-bytes write skip uses.

## [0.1.31] - 2026-11-04

Fewer copies per uniform buffer bind and fewer worker wake-ups: Donkey Kong Bananza's open map holds
25.2 fps with its 1% low up from 22.0 to 22.9 (facing away 53.9 -> 55.2; one run each), rendering
unchanged (baseline 0.949).

### Changed
- **A uniform buffer bound at a new address keeps no CPU copy until that address comes back.** Each
  slot's bytes were copied twice per change, to the stream and to a CPU copy for the next comparison,
  even for per-object data bound at another address every draw (24% of DK's binds), which never
  compared equal. Comparisons now only run against the same address. An address's second bind makes
  the CPU copy and its third can reuse, so stream copies rise (244k -> 306k a second) while total
  copies fall (~490k -> ~365k).
- **`EDEN_NX2_WAKE_BATCH` defaults to 16** (was 4): the wake system call was 1.4% of the GPU back thread.
  Submissions, present and waits still wake the worker at once.
- Vertex buffer binding clears its binding lists instead of constructing new ones.

## [0.1.30] - 2026-11-04

A cheaper register write path on the GPU back thread: Donkey Kong Bananza's open map 24.8 -> 25.3 fps
(~170k -> ~173k draws a second; one run each, so close to noise), rendering unchanged (baseline 0.952).

### Changed
- **The GPU split's fields no longer share cache lines between its threads.** `write_position`, which
  the front writes for every entry, shared a line with `ring` and `system`, which the back reads for
  every entry: the back took a cross-core cache miss to read two pointers that never change.
- **`Maxwell3D::CallMethodRun` runs a batch of macro writes in one call**, with plain state writes
  inline instead of one virtual `CallMethod` each, and prefetches the ring words ahead (the front wrote
  them on another core). Macros, executable methods and shadow RAM control still take `CallMethod`.
- The binding skip of 0.1.28 compares against the previous draw's entries where they already are in
  the descriptor queue, instead of copying every draw's payload.

### Added
- Profiler counters `uniform_new_address`, `uniform_clean`, `uniform_stream_written`,
  `uniform_unexplained` (small uniform binds; DK: 24% / 71% / 4.5% / 0%: its CPU never changes a
  uniform buffer that stays at the same address).

### Removed
- The `EDEN_NX2_REG_HISTOGRAM` diagnostic of 0.1.27 (a check on every register write).

### Internal
- Known issue: one benchmark run ended with a failed `stop` (no details kept; the process had gone
  afterwards). A later run under ProcDump hung: the game panicked in its audio thread during shutdown
  (`Userspace PANIC`, 2010-0212), the emulation thread missed its 11 s limit, and
  `MainWindow::TerminateEmulationThread` deadlocked inside `QThread::terminate` (the `finished` signal
  reaches `OnEmulationStopped`, whose `isRunning` waits on a mutex terminate holds). ProcDump slows the
  process, so that run may not be the same failure. A run without it stopped in 1.1 s with no panic.
  `.plans/dk-perf-scripts/stop_cycles.sh` and `sample_stop.py` help diagnose it if it comes back.

## [0.1.29] - 2026-11-04

An MCP tool that shows what the agent is doing in the status bar, and a fix for a per-draw cost the
draw merging of 0.1.28 added.

### Added
- **MCP `set_activity`**: a short text ("measuring with the key gate on") shown in the emulator's
  MCP status bar label as "doing: ..." (and in its tooltip with how long ago it was set), changeable
  at any time; an empty text clears it. Documented in `docs/MCP-tools.md`.

### Fixed
- **Every draw allocated memory when it was recorded alone.** Since 0.1.28 each draw is held back
  for merging and then recorded through the batch path, which copied the batch into a heap vector
  even for a single draw (every draw in Donkey Kong Bananza, ~165k a second). A single draw is now
  recorded as a plain draw. Open map 24.3 -> 24.8 fps (1% low 21.0 -> 22.1).

## [0.1.28] - 2026-11-04

Draw merging (plan C): consecutive draws of identical state are recorded as one multi-draw, and a
draw that binds exactly what the previous draw of its pipeline bound records no bindings. Built for
games that issue runs of identical-state draws (UI, text, particles, foliage); Donkey Kong Bananza's
draws each bring their own geometry, uniforms and base instance, so it merges none (bindings skipped
for ~2.7k draws a second), rendering unchanged (baseline 0.951).

### Added
- **Draw merging in the scheduler** (`Scheduler::RecordDraw`, `RecordDrawIndexed`,
  `RecordDrawIndirect`): a draw is held back while the next one may join it. It joins when nothing
  else was recorded in between (every other command records the held batch first, so nothing is
  reordered) and it draws the same way: same kind, instance count and first instance; indirect draws
  also read the record right after the batch's. A batch is recorded as one `vkCmdDrawMultiEXT` /
  `vkCmdDrawMultiIndexedEXT` (VK_EXT_multi_draw, now enabled when present) or multi-draw indirect
  call, or a loop of draws without them. Pipelines whose shaders read the draw index, and draws with
  transform feedback, are never merged. `EDEN_NX2_MERGE_DRAWS=0` turns it off.
- **Unchanged bindings record nothing**: while the previous draw of the same pipeline is still held
  (so nothing was recorded since), a draw whose descriptors, push constants, rescaling and descriptor
  buffer offset match it skips `ConfigureDraw`'s binding commands entirely.
- Profiler counters `merged_draws`, `skipped_bindings`.

## [0.1.27] - 2026-11-04

Batching (plan C), first piece: a draw whose pipeline state did not change reuses the previous
pipeline without rebuilding its key. In Donkey Kong Bananza 98% of draws keep the previous draw's
pipeline, textures and framebuffer; verified on the benchmark route (0 key mismatches, baseline
0.947); fps change within noise (24.3-24.9 vs 24.4).

### Changed
- **Pipeline key gate** (`PipelineCache::CurrentGraphicsPipeline`): a new dirty flag,
  `Dirty::PipelineKey` (dirty table 2), is set by every 3D register write except per-draw data the
  key never reads (constant buffer uploads and binds, draw parameters, index buffer, vertex stream
  addresses and limits, base vertex/instance, semaphores, shadow RAM control, macro upload). While
  it is clear, the shaders are unchanged and the topology and engine state match, the last lookup's
  pipeline is reused without `FixedPipelineState::Refresh` and the key comparison.
  `EDEN_NX2_KEY_GATE=0` turns it off; `EDEN_NX2_KEY_GATE_VERIFY=1` refreshes the key anyway and
  logs "Pipeline key gate mismatch" when it changed.

### Added
- Profiler counters `shape_same`, `merge_candidate`, `merge_contiguous` (C measurements: in DK
  98% / 3.4% / 0% of draws).
- Diagnostics: `EDEN_NX2_REG_HISTOGRAM=1` logs the 3D registers that mark state dirty most often
  (slow: for investigation only).

## [0.1.26] - 2026-11-04

Two shader recompiler bugs found while researching bindless textures.

### Fixed
- **The render area push constant overwrote the texture rescaling bits.** Both were declared at
  push constant offset 0, and the render area (pushed after the rescaling words) replaced the
  first 16 bytes of them, so a shader using the render area read garbage rescaling bits for its
  textures; a shader using both also had two push constant blocks, which Vulkan does not allow.
  The render area now lives at offset 32, after the rescaling layout (the pipeline's push constant
  range already covered it), and is a member of the rescaling block when a shader uses both.
- **Rectangle textures queried as 1D.** The texture type field of a texture instruction has 3 bits,
  so a TIC entry of type Color2DRect (8) assigned for a size query wrapped to Color1D; it is
  recorded as Color2D (a rectangle texture is a 2D view with unnormalized coordinates).

### Internal
- Known, not fixed (cannot be tested here): without robustness2 `nullDescriptor`, the fallback null
  image views are all 1D views whatever the binding's dimension.

## [0.1.25] - 2026-11-04

Bindless follow-up and a descriptor path diagnostic. Bindless stays off by default: on Donkey Kong
Bananza it is still ~0.5 fps behind the classic path (same build, two rounds: 25.1 / 25.3 vs 24.8 /
24.7 fps; `configure` 124 vs 132 ms/s on the GPU back thread), with identical rendering.

### Changed
- Bindless sampler slots are kept in each sampler variant after the first lookup
  (`Sampler::HeapSlotFor`) instead of a locked map lookup per texture on every slot table build.

### Added
- Diagnostics: the first graphics pipelines log their descriptor path (push descriptors,
  descriptor buffer or pooled sets, and whether bindless); `EDEN_NX2_FORCE_SETS=1` forces pooled
  sets to compare paths. On the test machine both classic and bindless pipelines use push
  descriptors; forcing pooled sets costs ~0.8 fps.

## [0.1.24] - 2026-11-04

Bindless textures work end to end (still off by default: `EDEN_NX2_BINDLESS=1`). Donkey Kong
Bananza renders correctly through the texture heap (baseline 0.946) at 24.5 fps on the open map,
against 25.0 with the classic path; tuning comes next.

### Added
- **Bindless graphics pipelines.** A graphics pipeline whose textures fit the slot table (at most
  4096 texture elements, no task/mesh stages; `UsesBindlessTextures`, decided the same way by the
  pipeline cache and the pipeline) samples them from the heap:
  - Shaders (`Profile::bindless_textures`): the module's own resources move to descriptor set 1;
    the heap's per-class image arrays and its sampler array are runtime arrays in set 0; textures
    are loaded through a slot table UBO (`uvec4 words[1024]`, one word per texture element: image
    slot | sampler slot << 18) declared where the first textured stage's textures were bound, with
    NonUniform decorations when the element index is dynamic.
  - Pipelines: layout {heap, own set}; the own set has the slot table instead of texture bindings
    (push descriptors or pooled sets at set 1; descriptor buffers are not used for these); the heap
    is bound whenever the pipeline is.
  - Draws: the slot table is built from the resolved views and samplers (`BuildBindlessTable`;
    reused on binding cache hits) and uploaded through the uniform stream, or the previous upload
    is bound again when the words and command buffer are the same.

## [0.1.23] - 2026-11-04

Groundwork for bindless textures (inactive unless `EDEN_NX2_BINDLESS=1`; nothing samples from the
heap yet). Design: a global texture heap indexed by shaders through a per-draw slot table.

### Added
- **Bindless device setup**: with `EDEN_NX2_BINDLESS=1` and a device that supports it, the
  descriptor indexing features bindless needs (runtime descriptor arrays, update-after-bind sampled
  images and samplers, update-unused-while-pending) stay enabled instead of being forced off, and
  the descriptor indexing limits are read (`Device::IsBindlessTexturesEnabled`,
  `DescriptorIndexingProperties`). It also needs partially bound bindings, non-uniform sampled image
  indexing and robustness2 null descriptors.
- **`TextureHeap`** (`vk_texture_heap.{h,cpp}`): one update-after-bind descriptor set with an array
  of sampled images per image class (dimension x float/uint colour, multisample, depth: 24 classes,
  `shader_recompiler/bindless.h`) and a sampler array. Slot 0 is a null descriptor; descriptors are
  written when a slot is allocated; released slots are reused only after the GPU passes their
  release tick. Image views own their slots (`TextureHeapSlots`, moved with the view, released in
  its destructor); samplers keep theirs for the heap's lifetime.

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
