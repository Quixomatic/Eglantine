# MCP Tool Reference

Every tool the built-in MCP server registers (`src/mcp/tools/*.cpp`), with its parameters and what
it needs before it runs. Setup and safety: [User Handbook - MCP](./user/MCP.md). Server layout and
how to add a tool: [MCP Server](./MCP.md). The short list with HTTP endpoints:
[`tools/mcp/README.md`](../tools/mcp/README.md).

The server also publishes this information at runtime: `tools/list` returns each tool's JSON schema,
and the `eglantine://tools/guide` resource lists every tool with its description.

## Gating

Each tool declares up to three requirements. The server checks them before the handler runs, in
this order, and refuses the call with a tool error (not a protocol error) when one is missing:

| Requirement | Meaning |
|---|---|
| writes | Allow Writes must be on (Tools > MCP Server > Allow Writes, the `mcp_allow_writes` setting, or `--mcp-allow-writes`). |
| game | A game must be running. |
| lease | The caller must hold the control lease. A free lease is taken automatically by the first call that needs it, every such call renews it, and it expires after 120 s without one (`acquire_control` can set 10 to 3600 s). Other sessions get a refusal that names the holder. |

Allow Writes covers input (buttons, sticks, touch, TAS, routes), guest memory writes, breakpoints,
settings changes (including determinism, debug knobs, dump switches and A/B tests) and docked
mode. Booting, pausing, resuming, stopping and quitting are not writes: they need only the lease.
Reading is never gated by Allow Writes.

A few tools check at runtime instead of declaring it: `act` needs writes and the lease only when it
is given `input`, and `debug_knobs` needs them only when the call changes a bit.

Tools that only touch the MCP's own data need neither writes nor the lease: saving screen labels,
probes, routes and baselines, recording a route, memory watches and scans, and switching IPC/SVC
tracing on or off.

All tools reject unknown arguments (`additionalProperties: false`). Results are JSON in
`structuredContent`, mirrored as a text block, plus PNG image blocks where noted.

In the tables below, "-" means the tool needs nothing.

## Shared argument types

Buttons (`press_buttons`, input steps): `A B X Y L R ZL ZR PLUS MINUS DUP DDOWN DLEFT DRIGHT LSTICK
RSTICK SL SR HOME CAPTURE`. TAS steps accept the same names except `HOME` and `CAPTURE`.

`player`: 1 to 8 for players 1 to 8 (default 1), 9 for handheld. TAS tools take 1 to 8; player 1's
script also drives handheld.

Input steps (`input_sequence.steps`, `act.input`, route step `input`): an array of 1 to 200 objects
`{buttons?, left?, right?, hold_ms?, wait_ms?}`. `left` and `right` are `[x, y]` in -1..1 (up is +y),
`hold_ms` (default 120) and `wait_ms` (default 0) are 0 to 30000. A whole sequence may last at most
60 s.

Timeline filter (`timeline`, `wait_for`, `act.wait.event`, `act.timeline`):

| Field | Type | Notes |
|---|---|---|
| `since_seq` | integer | Only events with a higher seq. Pass back the previous `next_seq`. |
| `kinds` | array | Any of `log state input mark ipc svc unimpl watch crash frame break`. Default all. |
| `min_level` | string | `trace debug info warning error critical`, for log events. |
| `source_prefix` | string | E.g. a log class such as `Service.NVDRV` or `Render`. |
| `contains` | string | Substring of the message. |

Value types (memory, probes, scans): `u8 u16 u32 u64 s8 s16 s32 s64 f32 f64`.

Addresses: the memory tools (`read_memory`, `write_memory`, `watch_add`) take an absolute virtual
address as a hex string (`"0x8004a000"`) or an integer. Probes and breakpoints also take
`Module+0xOFF` (stable across ASLR) and pointer chains such as `Module+0x10->0x8`.

Memory `region` (search and scans): `data` (writable regions, default), `heap`, `code` (r-x) or
`all` (all readable). Screen `region` (screen labels) is different: `[x, y, w, h]` in 0..1 of the
screen.

## Status and timeline

| Tool | Gating |
|---|---|
| `status` | - |
| `timeline` | - |
| `wait_for` | - |
| `mark` | - |
| `set_log_filter` | lease |
| `summarize_since` | - |
| `timeline_export` | - |
| `compare_runs` | - |

### `status`

Frontend, build and revision, whether MCP writes are on, the last timeline seq, and whether a game
runs. With a game: paused, program id, title, build id, presented frame count and docked mode.
Includes perf (game fps, speed %, frame time, shaders building) when the frontend has a sample. Call
it first. No parameters.

### `timeline`

Reads recorded events oldest first. Each event has `seq`, `t_ms`, `frame`, `kind`, `source`, `msg`
and, for logs, `level`. Returns `next_seq`; `dropped` reports events overwritten in the 65536-entry
ring before you read them, and `truncated` means `limit` cut the reply.

Parameters: the timeline filter, plus `limit` (1 to 2000, default 200).

### `wait_for`

Blocks until an event matches the filter or `timeout_ms` passes. Without `since_seq` only events
after the call starts count. Returns `{matched, event, next_seq}`; a timeout returns
`matched: false`.

Parameters: the timeline filter, plus `timeout_ms` (0 to 120000, default 10000).

```json
{"kinds": ["crash", "unimpl"], "timeout_ms": 60000}
```

### `mark`

Pushes a `mark` event (source `agent`) to bracket an experiment. Returns its seq.

Parameters: `label` (string, 1 to 500 chars, required).

### `set_log_filter`

Sets the log filter with the emulator's syntax: space-separated `<Class[.Sub]>:<Level>` rules applied
left to right, e.g. `*:Info Service.NVDRV:Debug`. Stores it in the `log_filter` setting and returns
the previous filter. Parse errors are not reported.

Parameters: `filter` (string, required).

### `summarize_since`

Digest since `since_seq`: event counts per kind, the most frequent clusters of similar events
(numbers collapsed, so repeats become one row with a count and frame range), every crash, newly
seen unimplemented commands, and current perf. Logs at warning and above by default.

Parameters: `since_seq` (integer), `min_level` (level), `top` (1 to 200, default 25).

### `timeline_export`

Writes the timeline since `since_seq` as JSONL to `<log dir>/mcp/<name>.jsonl`, for bug reports and
`compare_runs`.

Parameters: `name` (string, 1 to 64 chars, required), `since_seq`, `min_level`.

### `compare_runs`

Diffs two exports by event cluster: clusters only in `a`, only in `b`, and those whose frequency
changed more than 2x.

Parameters: `a`, `b` (export names, required), `limit` (1 to 500, default 30).

## Emulator lifecycle

| Tool | Gating |
|---|---|
| `list_games` | - |
| `boot_game` | lease |
| `pause` | game, lease |
| `resume` | game, lease |
| `stop` | game, lease |
| `quit_emulator` | lease |
| `game_info` | game |
| `firmware_status` | - |
| `set_docked` | writes, lease |

### `list_games`

Games in the frontend's game list: title, program id, path. eden-cli has no game list and cannot
boot: it runs the game given on its command line.

Parameters: `filter` (string, case-insensitive match on title or path).

### `boot_game`

Boots by file path, by program id (16 hex digits) or by a title that matches one game in the list.
Fails while a game runs (call `stop` first). Returns `since_seq` so the agent can follow the boot in
the timeline. When the GDB stub is enabled, the game waits for a debugger, so `boot_game` attaches
the MCP's debugger (up to 30 s) and the game starts.

Parameters: `path`, `program_id`, `title` (strings; give one).

```json
{"title": "Donkey Kong Bananza"}
```

### `pause`, `resume`, `stop`

Pause or resume the running game, or stop it. `stop` skips the confirmation prompt, so unsaved game
progress is lost. No parameters.

### `quit_emulator`

Stops the game without a prompt and closes the emulator; the MCP server shuts down with it. Useful
before a rebuild, since the executable cannot be replaced while it runs. In eden-cli, `stop` and
`quit_emulator` end the process. No parameters.

### `game_info`

Title, display version, developer, program id, build id and file type of the running game, plus its
icon as a PNG.

Parameters: `icon` (boolean, default true).

### `firmware_status`

Whether system firmware is installed, and its version. No parameters.

### `set_docked`

Switches between docked and handheld while the game runs, like the frontend's toggle (player 1
becomes a Pro Controller when docking from handheld).

Parameters: `docked` (boolean, required).

## Screen

| Tool | Gating |
|---|---|
| `screenshot` | game |
| `screenshot_raw` | game |
| `capture_frames` | game |
| `frame_advance` | game, lease |
| `screen_label` | game |
| `screen_state` | game |
| `screen_labels` | game |

### `screenshot`

PNG of the next presented frame as displayed, post-processing included. Fails if no frame is
presented within `timeout_ms` (paused or loading).

Parameters: `max_width` (160 to 3840, default 1280), `timeout_ms` (100 to 60000, default 5000).

### `screenshot_raw`

The last frame from the capture layer at 1280x720, without post-processing or upscaling. Works when
the window is hidden or the game is paused.

Parameters: `max_width` (160 to 1280, default 1280).

### `capture_frames`

A burst of `count` screenshots, one every `every_frames` presented frames, for watching an animation
or a glitch.

Parameters: `every_frames` (1 to 3600, default 30), `count` (1 to 12, default 4), `max_width` (160
to 1280, default 480).

### `frame_advance`

Pauses if needed, runs `frames` presented frames, pauses again and returns a screenshot. Accurate to
about a frame, since pausing is not frame-locked.

Parameters: `frames` (1 to 600, default 1), `timeout_ms` (100 to 60000, default 10000),
`screenshot` (boolean, default true).

### `screen_label`

Stores a small fingerprint of the current frame under a name for this game ("title", "loading",
"gameplay"...). Calling it again adds samples to the label. After that, `screen_state`, `act` and
routes recognise the screen without an image. Labels live in `<config>/mcp/screens/<program id>.json`.

Parameters: `name` (1 to 64 chars, required), `region` (`[x, y, w, h]`, match only this part of the
screen), `threshold` (0.5 to 1, default 0.9), `replace` (boolean: drop the old samples).

### `screen_state`

Matches the current frame against the game's labels and returns the best label, or `unknown`, with
scores. No parameters.

### `screen_labels`

Lists the game's labels (samples, threshold, region).

Parameters: `remove` (label name to delete).

## Input

| Tool | Gating |
|---|---|
| `press_buttons` | game, writes, lease |
| `set_stick` | game, writes, lease |
| `input_sequence` | game, writes, lease |
| `release_all` | game, writes, lease |
| `tap` | game, writes, lease |
| `swipe` | game, writes, lease |
| `get_controller_state` | game |
| `act` | game; writes and lease only with `input` |

The input tools use the virtual gamepad on wall-clock time. Physical controllers keep working, and
the last pressed source wins per button. Games read input once per frame, so holds shorter than
about 50 ms can be missed. For frame-exact input use the TAS tools.

### `press_buttons`

Presses buttons together, holds them, releases them, optionally repeated.

Parameters: `buttons` (1 to 8 button names, required), `hold_ms` (16 to 10000, default 120), `repeat`
(1 to 50, default 1), `gap_ms` (16 to 10000, default 120), `player`.

```json
{"buttons": ["A"], "hold_ms": 100}
```

### `set_stick`

Moves the left or right stick to `(x, y)` in -1..1 (up is +y). With `hold_ms` it returns to center
afterwards; without, it stays until moved again.

Parameters: `stick` (`left` or `right`, required), `x`, `y` (required), `hold_ms` (0 to 30000),
`player`.

### `input_sequence`

Runs input steps in order: each presses buttons and/or holds sticks for `hold_ms`, then waits
`wait_ms`. At most 200 steps and 60 s in total.

Parameters: `steps` (input steps, required), `player`.

```json
{"steps": [{"buttons": ["B"], "hold_ms": 100, "wait_ms": 500},
           {"left": [0, 1], "hold_ms": 1500}]}
```

### `release_all`

Releases every virtual button and centers both sticks on all players. No parameters.

### `tap`

Touches the handheld touch screen at `(x, y)` in 0..1 (top left is 0,0).

Parameters: `x`, `y` (required), `hold_ms` (16 to 10000, default 100), `repeat` (1 to 20, default
1), `gap_ms` (16 to 5000, default 150).

### `swipe`

Drags one finger from `from` to `to` over `duration_ms`.

Parameters: `from`, `to` (`[x, y]` in 0..1, required), `duration_ms` (32 to 10000, default 300),
`hold_end_ms` (0 to 5000, default 50).

### `get_controller_state`

Buttons and sticks as the game sees them, and whether the controller is connected. Use it to check
that input landed.

Parameters: `player`.

### `act`

Input, wait and observe in one call. Runs the optional `input` steps, then waits (for a timeline
event, then a number of guest frames, then milliseconds; default 10 frames), then returns what
`capture` asks for. Without `input` it needs neither writes nor the lease, so it also works as "wait
and look".

Parameters:

| Field | Notes |
|---|---|
| `input` | Input steps. |
| `player` | Player for the input. |
| `wait.event` | Timeline filter: wait for the first matching event. |
| `wait.frames` | 0 to 3600 guest frames (frame-locked). `wait.frames_timeout_ms` 0 to 120000, default 20000. |
| `wait.screen` | Array of screen labels: wait until `screen_state` is one of them. `wait.screen_timeout_ms` 0 to 600000, default 30000. |
| `wait.ms` | 0 to 60000. |
| `capture` | Any of `screenshot timeline controller perf screen_state probes`. Default `[screenshot, timeline, perf]`. |
| `timeline` | Timeline filter for the captured events (logs at warning and above by default). |
| `max_width` | Screenshot width, 160 to 3840, default 960. |

```json
{"input": [{"buttons": ["A"], "hold_ms": 100}],
 "wait": {"screen": ["gameplay"], "screen_timeout_ms": 60000},
 "capture": ["screen_state", "timeline"]}
```

## TAS

| Tool | Gating |
|---|---|
| `tas_status` | - |
| `tas_read_script` | - |
| `tas_write_script` | writes, lease |
| `tas_play` | game, writes, lease |
| `tas_stop` | writes, lease |
| `tas_record_start` | game, writes, lease |
| `tas_record_stop` | writes, lease |
| `tas_set_options` | writes, lease |

TAS input is one script line per presented guest frame, so it stays in step when frame times swing.
Scripts are the emulator's TAS-nx files in the TAS directory.

### `tas_status`

Engine state (stopped, running, recording), current script frame, loaded script length per player,
and options. No parameters.

### `tas_write_script`

Writes a player's script and reloads it. Give either `steps`, which the server expands, or a raw
TAS-nx `script` (`<frame> KEY_A;KEY_B <lx;ly> <rx;ry>` per line, axes -32767 to 32767, `NONE` for no
buttons). Then call `tas_play`.

Parameters: `player` (1 to 8), `steps` (1 to 5000 objects `{frames (1 to 216000, required), buttons?,
left?, right?}`), `script` (string, up to 4 MiB).

```json
{"steps": [{"frames": 30}, {"frames": 4, "buttons": ["A"]}, {"frames": 60}]}
```

### `tas_read_script`

Reads a player's script, or the last recording.

Parameters: `which` (`"1"` to `"8"`, default `"1"`, or `"record"`), `max_bytes` (1 to 1048576,
default 65536).

### `tas_play`

Plays the loaded scripts from frame 0, all players at once. By default waits until they end and
returns frames played, warning and higher timeline events, and screenshots taken when the script
reaches each frame in `screenshot_at` (within a frame or two).

Parameters: `wait` (boolean, default true), `timeout_ms` (1000 to 600000, default 120000),
`screenshot_at` (up to 12 script frames), `max_width` (160 to 3840, default 640).

### `tas_stop`

Stops playback; input returns to the other sources. No parameters.

### `tas_record_start`

Records player 1's input every frame, whether it comes from a person or the input tools. No
parameters.

### `tas_record_stop`

Stops recording and saves it as `record.txt`.

Parameters: `save_as_script` (boolean: also save it as player 1's script, ready for `tas_play`).

### `tas_set_options`

Sets `loop` (replay forever) and `enabled` (the TAS engine; the other TAS tools turn it on
themselves). Returns the previous values.

Parameters: `loop`, `enabled` (booleans).

## Routes and probes

| Tool | Gating |
|---|---|
| `route_save` | game |
| `route_list` | game |
| `route_run` | game, writes, lease |
| `route_status` | - |
| `route_stop` | lease |
| `route_record_start` | game |
| `route_record_stop` | - |
| `probe_save` | game |
| `probe_read` | game |

A route is a saved recipe for one game, such as `to_gameplay`. Routes are stored in
`<config>/mcp/routes/<program id>.json`, probes in `<config>/mcp/probes/<program id>.json`. When a
session ends, a background route it started is stopped.

Route steps (`route_save.steps`, `route_run.steps`, 1 to 100 objects):

| Field | Notes |
|---|---|
| `label` | Name shown in progress reports. |
| `input`, `player` | Input steps to run. |
| `wait_frames` | 0 to 36000 guest frames. |
| `wait_ms` | 0 to 600000. |
| `wait_screen` | Labels: wait until the screen is one of them. |
| `wait_not_screen` | Labels: wait until the screen is none of them. |
| `wait_probe` | `{name, op, value}`, `op` one of `equals not_equals greater less`. |
| `checkpoint` | Recorded fingerprint `{thumb, threshold, region?}`, waited for before the input. `route_record_stop` writes these. |
| `sample` | `perf_sample` arguments: measure when this step is reached (benchmark routes). |
| `repeat_until` | Repeat the input until the wait holds. `max_repeats` 1 to 200, `repeat_interval_ms` 100 to 60000. |
| `timeout_ms` | 0 to 600000. |
| `required` | Stop the route on timeout (default true). |

### `route_save`

Saves a named route for the running game.

Parameters: `name` (1 to 64 chars, required), `description`, `steps` (required).

```json
{"name": "to_gameplay",
 "steps": [{"wait_screen": ["title"], "timeout_ms": 60000},
           {"input": [{"buttons": ["A"], "hold_ms": 100}], "wait_screen": ["gameplay"],
            "repeat_until": true, "max_repeats": 10}]}
```

### `route_list`

The game's routes with their steps, and the file they live in.

Parameters: `summary` (boolean: step counts only), `remove` (route name to delete).

### `route_run`

Runs a saved route by name, or inline `steps`, and reports each step (attempts, screen reached,
checkpoint scores, skips and re-presses), plus a final screenshot. With `background: true` it
returns at once; follow it with `route_status` or timeline events from source `route`.

Parameters: `name`, `steps`, `screenshot` (boolean, default true), `background` (boolean).

### `route_status`

Progress of the running or last route: state, step and total, what it waits for, and per-step
results of a background run. No parameters.

### `route_stop`

Stops a route started with `background: true`. No parameters.

### `route_record_start`

Starts recording what is played, from any input source, into a route. Every frame's input is kept,
plus a screen fingerprint whenever input starts. Input after a settled screen becomes a checkpoint
(replay waits for that screen, then presses); input during motion is replayed on timing.

Parameters: `name` (1 to 64 chars, required).

### `route_record_stop`

Stops recording, turns it into route steps (checkpoints, timed input and an end-screen check) and
saves it. Returns the outline.

Parameters: `save` (boolean, default true), `description`, `threshold` (checkpoint match threshold,
0.5 to 1, default 0.85).

### `probe_save`

Names a game variable so it can be read in one call. `meanings` maps values to names, e.g.
`{"3": "title"}`.

Parameters: `name` (1 to 64 chars, required), `address` (absolute, `Module+0xOFF` or a pointer
chain, required), `type` (value type, default u32), `meanings` (object), `note`.

### `probe_read`

Current values (and meanings) of the game's probes, all or by name.

Parameters: `names` (array), `remove` (probe name to delete).

## Guest memory

| Tool | Gating |
|---|---|
| `read_memory` | game |
| `write_memory` | game, writes, lease |
| `search_memory` | game |
| `watch_add` | game |
| `watch_list` | - |
| `watch_remove` | - |
| `scan_start` | game |
| `scan_next` | game |
| `scan_results` | game |

### `read_memory`

Reads the application's memory as hex, ASCII, or `count` values of a type. At most 64 KiB. Fails if
the range is not fully mapped.

Parameters: `address` (required), `format` (`hex` default, `ascii`, or a value type), `length` (1 to
65536, default 64, for hex and ASCII), `count` (1 to 8192, for typed reads).

```json
{"address": "0x8004a000", "format": "f32", "count": 4}
```

### `write_memory`

Writes hex bytes or one typed value. Code writes invalidate the JIT cache for the range. The write
is logged to the timeline with the previous bytes.

Parameters: `address` (required), `hex` (up to 8192 chars), or `type` and `value`.

### `search_memory`

Finds a byte pattern (hex with `??` wildcards, e.g. `"DE AD ?? EF"`), an ASCII string, or a typed
value.

Parameters: `hex`, `ascii`, or `type` and `value`; `region` (memory region), `align` (1 to 4096,
default 1), `max_results` (1 to 10000, default 50).

### `watch_add`

Samples a value once per presented frame; each change becomes a `watch` timeline event (old and new
value, frame). Changes that revert within one frame are missed. Watches clear when the game stops.

Parameters: `address` (required), `type` (default u32), `label` (up to 100 chars).

### `watch_list`

Watches with their last value and change count. No parameters.

### `watch_remove`

Parameters: `id` (integer) or `all` (boolean).

### `scan_start`

Starts a Cheat Engine style search: every type-aligned address in the region that holds `value`
(floats within `tolerance`). Replaces any previous scan and stops at 10 million candidates.

Parameters: `value` (required), `type` (default u32), `tolerance` (default 0), `region` (default
`data`).

### `scan_next`

Keeps candidates whose current value passes the filter, and lists them once 20 or fewer remain.

Parameters: `filter` (required: `equals`, `not_equals`, `greater`, `less` with `value`; or
`changed`, `unchanged`, `increased`, `decreased` against the previous round), `value`.

### `scan_results`

Current candidates with their current and previous values. Does not change the scan.

Parameters: `limit` (1 to 5000, default 100).

## Process and threads

| Tool | Gating |
|---|---|
| `process_info` | game |
| `memory_map` | game |
| `list_threads` | game |
| `backtrace` | game, lease |

### `process_info`

Pid, name, program id, Switch 2 flag, address-space layout (alias, heap, ASLR, stack) and loaded
modules with base and end addresses. No parameters.

### `memory_map`

Mapped regions with base, end, size, permissions and state (Code, CodeData, Normal, Stack,
Shared...).

Parameters: `include_free` (boolean), `state` (string filter, e.g. `Normal`).

### `list_threads`

The application's threads: id, name, state, wait reason, priority, core and CPU time. No
parameters.

### `backtrace`

Symbolized backtraces (module, offset, symbol) of one thread or all. It pauses a running game
briefly, because the contexts of running threads are stale, so it needs the lease.

Parameters: `thread_id` (integer; omit for all threads).

## Services and tracing

| Tool | Gating |
|---|---|
| `unimplemented_commands` | - |
| `ipc_trace` | - |
| `ipc_summary` | - |
| `svc_trace` | - |
| `list_services` | game |

Newly seen unimplemented commands are always posted as `unimpl` events (service, command, request
words and the caller's backtrace), and guest crashes (`svcBreak`, the fatal service) as `crash`
events with the thread's backtrace, whether tracing is on or not.

### `unimplemented_commands`

Service commands the running software called that the emulator does not implement, with call
counts, whether they were auto-stubbed, the first call's request words and the caller's backtrace.

Parameters: `service_prefix` (e.g. `nx2:` or `appletOE`), `sort` (`first_seen` default, or `count`),
`backtraces` (boolean, default true), `reset` (boolean: clear the list after returning it).

### `ipc_trace`

Turns IPC tracing on or off. Each call becomes an `ipc` event with service, command id, handler
name, result word, calling thread and request words. Enabling resets the counters.

Parameters: `enable` (boolean, required), `service_prefix`, `errors_only` (only non-zero results),
`events` (boolean, default true; false counts calls for `ipc_summary` without posting events).

### `ipc_summary`

IPC calls counted since `ipc_trace` was enabled, grouped by service, command and result, most
frequent first. Finds a game polling something that never becomes ready, or a command that keeps
failing.

Parameters: `top` (1 to 1000, default 40), `reset` (boolean).

### `svc_trace`

Turns supervisor-call tracing on or off. Each SVC (optionally only the ids in `svcs`) becomes an
`svc` event with thread, inputs and outputs. Returns per-SVC counts.

Parameters: `enable` (boolean, required), `svcs` (up to 64 ids, 0 to 255).

### `list_services`

Services the emulator registered. Switch 2 services it lacks get stub sessions on demand and are not
listed; `ipc_summary` shows those in use.

Parameters: `prefix`.

## Breakpoint debugger

| Tool | Gating |
|---|---|
| `debugger_status` | - |
| `debugger_attach` | game, lease |
| `debugger_detach` | lease |
| `breakpoint_add` | game, writes, lease |
| `breakpoint_list` | - |
| `breakpoint_remove` | lease |
| `debugger_pause` | game, lease |
| `debugger_step` | game, lease |
| `debugger_continue` | game, lease |

The MCP uses the emulator's GDB stub as its debugger. The stub has to be on when the game boots
(`settings_set {"values": {"use_gdbstub": "true"}}`, then boot again). Only one debugger can be
attached, so an external GDB blocks the MCP and the other way round. When the server stops it
removes its breakpoints and detaches.

### `debugger_status`

Whether the stub runs, whether the MCP is attached, running or paused (and where), breakpoints with
hit counts, and recent hits with registers and backtraces. No parameters.

### `debugger_attach`

Connects to the stub; the game keeps running. `breakpoint_add` attaches by itself. No parameters.

### `debugger_detach`

Removes all breakpoints, lets the game run and disconnects. No parameters.

### `breakpoint_add`

Stops when code runs at an address (`exec`) or when memory is written, read or accessed (`write`,
`read`, `access`, over `size` bytes). In `log` mode each hit posts a `break` event with registers
and a symbolized backtrace, then continues; after `max_hits` the breakpoint retires. In `stop` mode
the game stays paused until `debugger_continue`.

Parameters: `address` (absolute, `Module+0xOFF` or a pointer chain, required), `type` (`exec`
default, `write`, `read`, `access`), `size` (1 to 8, default 4), `label`, `mode` (`log` default or
`stop`), `max_hits` (1 to 100000, default 1000).

```json
{"address": "main+0x1a2b40", "type": "write", "size": 4, "mode": "log"}
```

### `breakpoint_list`

Breakpoints and watchpoints with hit counts. No parameters.

### `breakpoint_remove`

Parameters: `id` (integer) or `all` (boolean).

### `debugger_pause`

Stops all guest threads through the debugger, like Ctrl-C in GDB, so `debugger_step` can step. No
parameters.

### `debugger_step`

Executes one instruction of the stopped thread and stays paused; returns registers and a backtrace.
No parameters.

### `debugger_continue`

Resumes a game the debugger paused, stepping over the breakpoint it stopped on. No parameters.

## Dumps and reports

| Tool | Gating |
|---|---|
| `dump_set` | writes, lease |
| `dump_list` | - |
| `dump_read` | - |
| `report_list` | - |
| `report_read` | - |
| `fs_access_log` | - |

These read only the files the emulator writes to its dump and report directories.

### `dump_set`

Turns dumping of guest shaders (`.ash`, for nvdisasm), SPIR-V (`.spv`) and GPU macros on or off.
Returns the state and dump directory. Shaders are dumped when they compile, so clear the shader
cache or reach new content.

Parameters: `shaders`, `spirv`, `macros` (booleans).

### `dump_list`

Files in the dump directory, newest first, with size and age.

Parameters: `since_ms_ago` (integer), `subdir`, `contains` (path filter, e.g. `.spv`), `limit` (1 to
1000, default 100).

### `dump_read`

Reads part of a dump file by its `dump_list` path, as text or hex. Page with `offset` and the
returned `next_offset`.

Parameters: `path` (required), `offset` (default 0), `length` (1 to 65536, default 4096), `format`
(`auto` default, `text`, `hex`).

### `report_list`

Reports the emulator saved as JSON, newest first. They are written only while `reporting_services`
is on.

Parameters: `type` (`crash_report`, `svc_break_report`, `unimpl_func_report`,
`unimpl_applet_report`, `error_report`, `play_report`, `user_report`), `since_ms_ago`, `limit` (1 to
1000, default 100).

### `report_read`

One report as parsed JSON (registers, backtrace, IPC command buffer, error text...).

Parameters: `type` and `file` (from `report_list`, required), `max_bytes` (1024 to 4194304, default
262144).

### `fs_access_log`

Lines of the filesystem access log a game writes through `OutputAccessLogToSdCard`. Needs
`enable_fs_access_log` on before boot.

Parameters: `since_line` (pass back `next_line`), `contains`, `limit` (1 to 2000, default 200).

## Performance

| Tool | Gating |
|---|---|
| `perf_sample` | game |
| `gpu_profile` | - |
| `baseline_save` | game |
| `baseline_check` | game |
| `ab_test` | game, writes, lease |
| `ab_test_status` | - |
| `ab_test_stop` | lease |

### `perf_sample`

Measures for `seconds`: frame pacing (average fps, median, p95, p99 and worst frame time, 1% low), a
GPU-thread profile of the same window, host CPU per emulator thread, and either a baseline check or
a screenshot. Run it at the same route checkpoint before and after a change.

Parameters: `seconds` (2 to 60, default 10), `profile` (boolean, default true), `baseline` (name),
`baseline_threshold` (0 to 1, default 0.9), `screenshot` (boolean, default true), `label` (up to 100
chars).

### `gpu_profile`

The NX2 GPU-thread profiler as data: per thread, exclusive ms/s and calls/s per zone (draw, macro,
update buffers, split drain...), counters (draws, register writes, downloads), split drains by call
site, and fps. `enable` switches the profiler on or off live. `fresh` waits for a report that covers
only time after the call (default when the call just enabled it).

Parameters: `enable` (boolean), `window_ms` (1000 to 60000, default 10000), `fresh` (boolean),
`top_zones` (0 to 64, 0 = all).

### `host_profile`

A sampling profiler for emulator host threads, for finding the functions behind a `gpu_profile`
zone. It pauses the named threads about 1000 times a second, unwinds their stacks and symbolizes
them from the PDB next to the executable. Per thread it returns `self` (the innermost function),
`self_caller` (innermost function and its caller) and `inclusive` (every function on the stack),
each as a percentage of that thread's samples. Thread names are the ones `perf_sample` lists under
`host_threads`, matched exactly or by prefix with a trailing `*` (`CPUCore_*`). Windows only; it
needs no game, writes or lease. The same sampler runs in eden-cli from `EDEN_NX2_SAMPLE`.

Parameters: `threads` (array of names, default `["GPU", "GPUBack"]`), `seconds` (1 to 30,
default 5), `top` (1 to 200 rows per list, default 25), `folded` (boolean: adds every distinct stack
as `thread;outer;...;inner count` lines, for flame graphs).

```json
{"name": "host_profile", "arguments": {"threads": ["GPU", "GPUBack"], "seconds": 10}}
```

### `baseline_save`

Saves the current frame as a named known-good image for this game (PNG and fingerprint under
`<config>/mcp/baselines/`).

Parameters: `name` (1 to 64 chars, required).

### `baseline_check`

Scores the current frame against a baseline (1.0 is the same picture; below `threshold` the
rendering changed) and returns the current image.

Parameters: `name` (required), `threshold` (0 to 1, default 0.9).

### `ab_test`

For each of 2 to 6 values of a setting: sets it, reboots the game if needed, then runs `route` (a
saved benchmark route with `sample` steps) or a `perf_sample` of `sample_seconds`. Restores the
original value at the end, also on failure. Returns a table (fps_avg, one_percent_low,
frame_ms_median, frame_ms_p95, speed_pct, route_ok, notes) and the best value by fps. With
`background: true` it returns at once. When a session ends, its A/B test is stopped.

Parameters: `setting` (required), `values` (2 to 6 strings, required; enums take names or indices),
`route`, `sample_seconds` (2 to 60, default 8), `boot_route` (route from a fresh boot to the
measuring point, run after each reboot), `reboot` (`auto` default: only when the setting cannot
change at runtime; `always`; `never`), `background` (boolean).

```json
{"setting": "resolution_setup", "values": ["Res1X", "Res2X"], "route": "bench_spawn",
 "boot_route": "to_gameplay"}
```

### `ab_test_status`

Progress of the running A/B test, or the full result of the last one. No parameters.

### `ab_test_stop`

Ends the running A/B test early. It still restores the original value (rebooting back if it
rebooted) before reporting `stopped`. No parameters.

## Settings

| Tool | Gating |
|---|---|
| `settings_list` | - |
| `settings_get` | - |
| `settings_set` | writes, lease |
| `debug_knobs` | writes and lease only to change |
| `set_determinism` | writes, lease |
| `env_switches` | - |

Settings are addressed by config key. Web service settings, anything with `token`, `password` or
`serial` in its name, and the MCP's own settings (`enable_mcp_server`, `mcp_*`) are hidden: they are
not listed and cannot be read or set.

### `settings_list`

Settings and current values by category.

Parameters: `filter` (name substring), `category` (e.g. `Renderer`, `Core`, `System`, `Debugging`),
`changed_only` (boolean: only non-default values).

### `settings_get`

Value, default, type, range, whether it can change while a game runs, and the per-game override
state.

Parameters: `names` (1 to 100 keys, required).

### `settings_set`

Sets settings by key. Enums take their names or indices. Applied to the running game where possible;
the others report `reboot_needed`.

Parameters: `values` (object of key to value, required).

```json
{"values": {"resolution_setup": "Res1X", "use_vsync": "Fifo"}}
```

### `debug_knobs`

Reads or changes the 16 debug knob bits that experimental code checks with
`Settings::GetDebugKnobAt(i)`. `value` replaces all bits, then `set` and `clear` apply.

Parameters: `value` (0 to 65535), `set`, `clear` (arrays of bit numbers 0 to 15).

### `set_determinism`

Makes runs repeatable: fixed RNG seed, fixed RTC (unix seconds), language and region (names or
indices). `clear: true` turns the seed and RTC back off. Most of these take effect on the next boot.

Parameters: `rng_seed` (0 to 4294967295), `rtc` (integer), `language` (e.g. `EnglishAmerican`),
`region` (e.g. `Usa`), `clear` (boolean).

### `env_switches`

The emulator's `EDEN_*` environment variables. Other variables are never returned.

Parameters: `prefix` (must start with `EDEN_`).

## Multiple agents

| Tool | Gating |
|---|---|
| `control_status` | - |
| `acquire_control` | - |
| `release_control` | - |
| `set_activity` | - |

### `control_status`

Whether another session holds the control lease. No parameters.

### `acquire_control`

Takes the lease explicitly, so no other agent changes state while you work. A single agent does not
need this. Fails if another session holds it.

Parameters: `label` (your name, shown to other agents, up to 80 chars), `ttl_s` (10 to 3600,
default 120).

### `release_control`

Gives up the lease. The lease is also released when the session ends. No parameters.

### `set_activity`

Shows a short description of what the agent is doing right now ("measuring with the key gate on")
in the emulator's status bar (`doing: ...`, with the time it was set in the tooltip), so the person
watching knows. Call it again to change it at any time; an empty `text` clears it.

Parameters: `text` (up to 120 chars).
