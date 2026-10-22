# Memory growth: "Elite leaks memory, especially in VR"

## Status

- **State (2026-10-22): static analysis only, no flight.** Nothing that grows
  at gigabyte scale was found in Elite's VR code (build 334473, OpenVR and
  LibOVR paths) or in EDVR's own source. Three small leaks in Elite are
  confirmed in code (below); none is large enough to explain the reports.
- **Open hypotheses:**
  - H1: the growth is in the VR runtime's own process (`OVRServer_x64` after
    Meta Link v74, or `vrserver`), which a player reading Task Manager's total
    sees as Elite. This is the best match for the one VR-specific report.
  - H2: Elite's texture streaming or map screens hold memory (forum anecdote:
    galaxy and system map textures are never freed).
  - H3: Elite's CPU heaps grow regardless of VR.
  - H4: EDVR's bounded retention (engine-motion pools and shaders) is larger
    in practice than the source suggests.
- **Evidence gap:** EDVR does not log process RAM at all. The `vram:` line
  stops when the gfx log reaches its 16 MB cap. No log in hand covers a
  multi-hour session.
- **Next:** the memory line proposed below (not built), then one VR flight of
  two hours or more that includes map screens, jumps, and taking the headset
  off and on.
- Ruled out: see "Ruled out" at the end.

Evidence files (scripts, query logs, decompiles) are under
`analysis/leak-2026-10-22/`, which is local and not in the repository.

## What players report

Research of the Frontier tracker, forums and patch notes, 2021-2026:

- No patch note from 2023 to 2026 mentions a memory leak. Odyssey's launch
  notes fixed a memory stomp and a video-memory streaming budget; Update 17
  reduced Titan audio memory.
- The only VR-specific report is tracker
  [74677](https://issues.frontier.co.uk/issue-detail/74677) (Quest 2 on Link,
  April 2025): system RAM rises 1-2% a minute, even at the main menu, stops
  when the headset is taken off and resumes when it is worn. It expired
  unconfirmed and does not say which process grew. Meta's Link v74 (March
  2025) was reported leaking RAM through `OVRServer_x64` in DCS and MSFS in the
  same weeks.
- The only acknowledged memory issue is
  [82038](https://issues.frontierstore.net/issue-detail/82038): a 22-32 GB
  runaway spike when a second wingmate drops into a Thargoid signal. It is a
  spike, not slow growth, and not VR.
- Nobody has traced the growth to a DLL or subsystem.

## Static findings in Elite (build 334473)

RVAs against image base `0x140000000`.

| Finding | Where | Size | Confidence |
|---|---|---|---|
| The Wwise controller-enumeration thread (`AkGameControllerEnum`) leaks its thread handle on every device removal, and on an arrival that does not match a known device | create `0x4d04c90`, thread proc `0x4d044e0` zeroes the handle without `CloseHandle` | one kernel thread object per event; not process heap | confirmed in code |
| The 1x1 RGBA8 texture made for `SetSkyboxOverride` is never released | `0x4e5a00` | one tiny texture per loading transition (three in a logged session) | confirmed in code |
| A failure partway through LibOVR swap chain creation skips the destroy and keeps the chains already made | `0x4e5360`, guard `+0x32c` | once, on a failure path | confirmed in code; inert under EDVR, which keeps Elite off LibOVR |
| The engine's thread creator discards its handle | `0x543f90`, five callers | probably long-lived threads | plausible |

What Elite uses from OpenVR: `IVRSystem_012`, `IVRCompositor_014`,
`IVRChaperone_003`, `IVRExtendedDisplay_001` and `IVROverlay_011` (debug
keyboard only). It has no render model, mirror texture, input, settings or
texture-load interface, so those classic acquire/release leaks cannot occur.

## Elite's own memory accounting

The retail exe keeps live GPU resource accounting, a possible source for an
in-process memory line. All offsets are build-bound.

- Device: `qword [0x5f1dca8]` + `0x18` = device D (null before the device
  exists).
- D+`0x1ee0`: 12 u64 live byte counters by type, in this order: RenderTarget/UAV,
  DepthTarget/UAV, Texture/UAV, Immutable, Texture, Vertex, Index, Buffer,
  Shader, Descriptor, CommandList, Unknown. Updated on allocation (`0x51fa70`)
  and free (`0x51fbf0`) under the critical section at D+`0x1ea0`.
- D+`0x1f40`: the high-water mark of their sum. D+`0x1f48`: a second set of 12
  that excludes resources with flag `0x40000`.
- D+`0x1ff8`: GPU heap count; D+`0x2000`: heap pointers. A heap has used bytes
  at `+0xb0`, its high water at `+0xb8`, capacity at `+0x4c8`, largest free
  block at `+0x4e8` and type at `+0x510`.
- The game's own report formats this table in KB (`0x519160`), and the crash
  log path (`0x520e90`) reaches it.

CPU side, plausible but not checked at run time: the heap manager at
`[0x5f1de58]` (static object `0x5f28120`) lists heaps at `+0x1400`, 16 bytes
each, with the count at `+0x1510`. Bytes obtained from the OS are at heap
`+0x60`. The main allocator's statistics block is at `*(heap+0x1a8)`, with
current bytes at `+0x10`, the peak at `+0x18` and the live count at `+0x20`.
Heap names are discarded in retail, so there are no categories.
`fHeapVirtualStore` always reports 0.

## EDVR's own retention (source audit)

No unbounded growth was found. EDVR never hooks `Release` and never wraps D3D
objects, so it can keep a game object alive only through a held reference in
a cache, and every cache found is capped. The largest bounded holds:

- `engine_velocity.cpp:3098-3123`: up to 16 of the game's stride-336 buffers,
  plus up to 32 copy pools, until engine motion stands down. Stale buffers are
  pinned only if the game recreates the pool.
- `engine_velocity.cpp:134-144`: up to 512 vertex and 512 pixel shaders with
  bytecode copies.
- `light_grid_cache_core.h`: 32 slots of the game's cluster tables, up to
  about 32 MB of game heap plus GPU buffers.

## Existing logs

The `vram:` lines (local GPU memory) were read from 81 game-directory and
`analysis/` logs and seven logs players sent. Only four span 30 minutes or
more:

- Two VR sessions: `edvr_gfx_20261016_203043` (72 min) and
  `edvr_gfx_20261021_092731` (Quest 3 on Link, 47 min). Neither climbs. Each
  peaks early (9.3 GB at minute 13; 20.6 GB, 94% of budget, at minute 6),
  then holds about 5.5 GB and 6.0 GB.
- Flat sessions: `20261019_082411` rises in steps, about 620 MB an hour over
  57 minutes, before the log cap stops it. `20261021_123140` saw-tooths
  between 3 and 9 GB, and its drops coincide with drops in the OS budget.

## Ruled out

- ruled out (static): leaks of OpenVR mirror textures, render models,
  overlays or loaded textures in Elite, because the exe requests none of those
  interfaces.
- ruled out (static): LibOVR per-frame buffer references, because Elite
  fetches each swap chain buffer once at creation and releases every one in
  its destroy (`0x4e5e50`). The per-frame submit (`0x4e14b0`) only copies and
  commits.
- ruled out (static): allocation on headset visibility changes, because the
  session status handler (`0x4e5150`) only sets flags.
- ruled out (static): OpenVR Submit creating objects each frame, because
  Submit (`0x4e19c0`) takes reference-counted holders with balanced counts.
- ruled out (static): growth in the VR error logs, because both are deques
  capped at 20 that free their strings.
- ruled out (static): leaks on audio default-device changes
  (`IMMNotificationClient`, `0x4a2bac0`), in DirectInput hot-plug
  re-enumeration (de-duplicated, at most 16 joysticks) and in SetupDi
  enumeration.
- ruled out (source): a per-frame, per-draw or per-event COM reference leak in
  EDVR, from an audit of about 1300 getter, AddRef and Attach sites.

## Proposed instrument (not built)

A `mem:` line on the same clock as `vram:`, in a budget that survives the gfx
log cap, carrying:

- process private bytes, commit (`PagefileUsage`) and handle count;
- Elite's 12 live GPU byte counters and their high water, read from the
  device above;
- after a first check of their values, the CPU allocator's current bytes.

A climb in private bytes with flat GPU counters points at H3. A climb in one
GPU type points at H2, and names the type. If Elite's own numbers hold flat
while the machine runs out of memory, the growth is in another process (H1);
the next step would then be to sample `OVRServer_x64` and `vrserver` by name.
