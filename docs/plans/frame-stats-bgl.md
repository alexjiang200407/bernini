# Frame statistics at the render target

## Context

The reported symptom — the material editor's frame counter is wrong — turned out to be a wiring bug
and is fixed separately in #452. The status bar was connected to `m_LevelEditor` alone, and the three
viewport docks are tabbed together with `DriveViewportsFromTab` dropping the unselected ones out of
the frame loop, so leaving the Level Editor tab stopped the emissions feeding the label and left its
last text on screen. What you read while working on a material was the level editor's frame time,
frozen at the tab switch. That needed none of this feature.

What is left is what the number *means*, and it is the reason to measure at the render target rather
than around it. `RenderTargetWindow::ReportFrameTiming` times the interval between one viewport's
draw callbacks — wall clock, on a thread whose time is mostly spent waiting. `RenderContext::
BeginFrame` blocks on that target's frame fence from two submissions back, and `PresentAndAdvance` is
called inside `EndFrame`, which on D3D12 is `Present(1, 0)`. So a single figure absorbs the
recording, the GPU backpressure and the vblank, and none of them can be told apart.

That is what makes the readout useless for the question it exists to answer. A material preview that
drags gives you one number that went up, with nothing to say whether the shader got expensive, the
CPU-side recording got expensive, or the frame is simply waiting on a display it was always going to
wait for. Separating the three needs the GPU's own clock, which only `bgl` can reach.

## Decisions

**ADR-1 — The RHI seam is "GPU time for this frame's submission", not a general timestamp-query
primitive.** *Rejected: a query-heap abstraction mirroring D3D12 and Vulkan, which is what Unreal's
RHI exposes.* Metal already carries whole-command-buffer GPU time — `MTL::CommandBuffer::GPUStartTime`
and `GPUEndTime`, readable once the buffer completes, with no query heap, no readback buffer and no
resolve. A query-heap seam would force Metal onto `MTLCounterSampleBuffer` to obtain a number it
already has.

The rejected alternative is stronger than that alone makes it sound, and the reasons it still loses
are worth writing down. Vulkan is on the roadmap and its query pool is shaped like D3D12's heap, so
the query form is the convention in two of three backends and Metal is the outlier. Per-pass GPU
time is also the obvious next question once a whole-frame figure exists, and the FrameGraph already
names its passes.

What decides it is that the two **compose rather than compete**: whole-frame GPU time is a timestamp
at list open and one at list close, so if a query heap is built later, `GetFrameStats`' GPU figure is
reimplemented on top of it and the public seam does not move. This is therefore not a case of two
ways into the library doing one thing. Per-pass timing is speculative today, and building its
machinery on Metal — where the frame-level number is free — to serve nothing is the cost that is not
worth paying yet. Widening later is reachable, not blocked: metal-cpp ships `MTLCounters.hpp`, and
`MTL::Device::supportsCounterSampling` makes support detectable rather than silent.

**ADR-2 — CPU time, GPU time, blocked time and completed-frame rate are four numbers and are never
blended.** *Rejected: a single "frame time".* This is the standard split — Unreal's `stat unit`
reports Frame / Game / Draw / GPU separately, and Unity's `FrameTimingManager` separates CPU main,
CPU render and GPU for the same reason. It is also the precise shape of the bug: today's readout is
a blend, a loop period sold as one viewport's frame time. See ADR-10 for what "CPU" and "blocked"
mean, which is the part that decides whether this split is real or cosmetic.

**ADR-3 — Statistics are opt-in, through `RenderTargetDesc`.** *Rejected: always on.* Headless
targets serve `bgl_tests` and asset cooking, where nobody reads the number and the D3D12 path would
still pay a `ResolveQueryData` and a readback every frame.

**ADR-4 — `bgl` owns the smoothing window; the client only displays.** *Rejected: raw per-frame
values out of `bgl`, smoothed by each client.* One rule for every client, and it deletes the
editor's own `core::RollingWindow<120>`. `core::RollingWindow` already exists and `bgl` links
`core`, so nothing new is written for it.

**ADR-5 — A stats read never blocks, and lags by the frame ring's depth.**
`IRenderTarget::GetFrameStats` returns the most recent measurement whose fence has completed.
*Rejected: flushing the queue so this frame's GPU time is available now* — a blocking read changes
the thing being measured. Unity documents `FrameTimingManager` as lagging for the same reason.

**ADR-6 — `bgl` measures; the client draws.** *Rejected: an FPS overlay drawn inside `bgl` into the
target.* `bgl` has no text rendering: an overlay means a font atlas and a glyph pass, which is large
new surface for a debug readout, and every client that wants the number already has a UI.

**ADR-7 — The render loop is not touched.** *Rejected: changing how viewports are scheduled.* There
is nothing to fix there: the docks are tabbed and, after #452, held that way, so exactly one viewport
is in the loop and the loop's period is that viewport's frame period. This feature changes what the
number means, not how frames are paced.

**ADR-8 — the rate figure counts frames *completed*, not frames presented.** *Rejected:
counting presents.* Neither backend presents every frame: `RenderTarget_d3d12.cpp` skips
`Present(1, 0)` entirely when headless, and `RenderTarget_metal.cpp` returns early from
`PresentToLayer` both when headless and — on a windowed target — whenever `nextDrawable()` hands back
null because the display holds every drawable, dropping that frame. A presented-frame count would
therefore read zero on every headless target and be silently lossy on Metal, so no single assertion
could test it on both backends. Completed frames are counted where `PresentAndAdvance` is called,
which is unconditional. The readout must not be labelled as frames reaching the display, because it
is not that.

**ADR-9 — the missed-vblank counter is dropped, not moved down.** *Rejected: carrying it into
`FrameStats` as a further field.* Today's counter tests one blended CPU figure against a fixed 20 ms
threshold — precisely the measurement ADR-2 removes, and a threshold that means nothing on a
headless target or on a display that is not 60 Hz. With CPU and GPU separated, a client that wants
"this frame overran a refresh" can ask it of the numbers it already has, against whatever interval
its own display runs at.

**ADR-10 — CPU time is *recording* time: wall clock minus the blocking regions, which are themselves
measured and reported.** *Rejected: wall clock across `BeginFrame`..`EndFrame`.* That span contains
three different things — the fence wait at the top of `BeginFrame`, the recording work, and
`PresentAndAdvance` inside `EndFrame`, which is `Present(1, 0)` on D3D12. Reporting their sum as
"CPU time" would reproduce this feature's own bug one layer down: better attributed, equally
blended. Unreal does not report raw thread wall clock either — it tracks thread idle explicitly and
subtracts it, so `stat unit`'s Draw is render-thread time minus time blocked on fences and events.

Both blocking regions are inside `bgl` and both are bracketable, so the subtraction is measured
rather than estimated. The subtracted total is exposed as `blocked` rather than discarded:
*Rejected: subtract and throw away, inferring GPU-bound by comparing GPU time against the frame
period.* That inference is possible but it makes waiting for the display and waiting for our own GPU
equally invisible, and with a two-deep ring the two are both common and mean opposite things. CPU
2 ms / GPU 14 ms / blocked 11 ms states the diagnosis outright. The two waits are reported as one
figure and not split: *rejected as five numbers on a status bar* — the split belongs to a profiler,
which is a non-goal.

**ADR-11 — the CPU figure is `bgl`'s recording cost for this target, and the editor measures its
loop separately.** *Rejected: one "CPU time" implying the viewport's whole cost to the application.*
`bgl` can only see the span it owns, `BeginFrame`..`EndFrame`. That is the right per-viewport scope —
`RenderTargetWindow::DrawFrame` is not virtual and nothing overrides it, so the editor's own
per-viewport work is a `RenderJob` struct copy — but it leaves one path uninstrumented: closures
posted through `Renderer::Post`/`Invoke` run on the render thread's event loop *between* `Frame()`
calls, inside no target's frame. A material graph recompile lands exactly there. Uninstrumented, a
stuttering preview would show four healthy per-target numbers and only a sagging frame rate, which
is this feature's own bug wearing a new disguise. So the editor times a whole `Renderer::Frame`
iteration and the gap between iterations; what that exceeds the sum of the per-target figures by is
render-thread work belonging to no viewport. This is Unreal's Frame-versus-Draw distinction, and it
is editor-side only — no `bgl` surface.

**ADR-12 — the completed-frame rate stays per target, and the editor shows the active one's.**
*Rejected: an application-level rate owned by the editor instead.* One viewport renders at a time, so
the active target's rate *is* the loop's rate and there is no second figure to reconcile. Keeping it
on `IRenderTarget` is what makes it right for a client that is not the editor, where a single target
is the whole application.

## Non-goals

- **Per-pass or per-FrameGraph-node GPU breakdown.** Whole-frame numbers only. The equivalent of
  Unreal's `stat GPU` is not built here, and ADR-1 accepts that widening the seam is its price.
- **Any change to `Renderer::Frame`, viewport scheduling, or the present path.** No throttling, no
  focus-only drawing, no change to `Present(1, 0)` or `presentDrawableAfterMinimumDuration`.
- **An FPS overlay drawn by `bgl`,** and no text rendering added to `bgl`.
- **Per-viewport overlay labels in the editor.** The status bar is the only readout; it follows the
  rendering viewport, with the loop figure of ADR-11 beside it.
- **A profiler UI:** no history graph, no capture to file, no per-frame log, and no split of
  `blocked` into its fence and present halves (ADR-10).
- **Vulkan.** No backend exists to implement the seam in.
- **CPU-side breakdown into game/draw/render phases.** One CPU number per frame per target.

## Acceptance

- `just test bgl` green, including new cases on a **headless** target with statistics enabled:
  after the frame ring's lag, `GetFrameStats` reports a GPU time greater than zero; a frame drawing
  many instances reports a larger mean GPU time than a near-empty one; and a target created without
  the flag reports statistics unavailable while recording no timestamps.
- `just run bgl_tests -- --gpu-validation` clean. The D3D12 task adds a query heap, a
  `ResolveQueryData` and a readback barrier, which is exactly what GPU validation catches.
- `just test editor` green.
- In the running editor, the status bar shows ADR-2's four figures for the rendering viewport, and
  the CPU and GPU halves move independently — a heavier shader moves GPU while CPU holds, which is
  the split the old single figure could not show.

## What the survey found

**The frame ring already exists on the target, and it is the right home.** `RenderTargetBase`
(`libs/bgl/src/gfx/RenderTargetBase.h`) owns `GetFrameIndex`, `GetFrameFence`/`SetFrameFence`,
`GetFrameAllocator`, `GetLastPresentedIndex` and `GetFrameCount` — a per-frame-in-flight slot with a
fence already attached to it. A per-slot timing record is an addition to a structure that already
tracks exactly what the readback needs to know.

**One command list per frame, one fence per frame.** `RenderContext::EndFrame`
(`libs/bgl/src/gfx/RenderContext.cpp`) submits with a single
`m_CommandQueue->ExecuteCommandList(m_CommandList)`, records the returned fence via
`rt.SetFrameFence(index, frameFence)`, then calls `rt.PresentAndAdvance()`. So a frame's GPU work is
one submission, identified by one fence value — which is what ADR-1's seam keys on.

**`BeginFrame` is where the CPU waits.** `RenderContext::BeginFrame` blocks on
`GetFrameFence(index)` before resetting the allocator and opening the list, and `PresentAndAdvance`
is called *inside* `EndFrame`. Any CPU number measured across that span therefore contains a wait for
GPU work two frames old plus (on D3D12) the present block. The ring is two deep
(`c_SwapchainImageCount = 2`, `libs/bgl/src/constants/constants.h`), so the CPU is on a short leash:
when the target is GPU-bound that wait is most of the frame, not a rounding error. ADR-10 is what
defines the CPU figure against this.

**There is no timestamp infrastructure in `bgl` at all.** Nothing under `libs/bgl/src` matches
`QueryHeap`, `timestamp` or `D3D12_QUERY`. `ROADMAP.md` lists "Readback ring — N buffers,
persistently mapped, fenced; never map a buffer written this frame" as an unchecked RHI item; the
D3D12 half of task 1 is a narrow, two-slot instance of that.

**The two backends are asymmetric, and the survey confirms both halves.**
`libs/bgl/src/metal/cmd/CommandQueue_metal.cpp` commits a `MTL::CommandBuffer` per submission, and
metal-cpp exposes `GPUStartTime()`/`GPUEndTime()` as `CFTimeInterval` on it — so Metal needs only to
retain the buffer until its fence completes. `libs/bgl/src/d3d12/cmd/CommandQueue_d3d12.cpp` has no
equivalent and needs the full query-heap path.

**Present differs between backends, which the cadence number must respect.**
`RenderTarget_d3d12.cpp` calls `Present(1, 0)`, which blocks the calling thread on the vblank.
`RenderTarget_metal.cpp` calls `presentDrawableAfterMinimumDuration(drawable, 1.0/60.0)`, a
scheduling hint that does not block. The same cadence figure will therefore be reached by different
mechanisms. Worse, neither presents unconditionally: D3D12 skips `Present` when headless, and Metal
skips it when headless *or* when `nextDrawable()` returns null on a windowed target, dropping the
frame. Hence ADR-8 — the figure counts completed frames, and nothing about it may assume a present
happened at all.

**One viewport renders at a time, and #452 already made the readout follow it.**
The three viewport docks are tabbed together and `DriveViewportsFromTab` connects each dock's
`visibilityChanged` to `SetRenderingEnabled`, so an unselected tab's viewport leaves the frame loop;
#452 additionally drops `Movable` and `Floatable` from those docks so they cannot be rearranged into
a second simultaneously-visible viewport. It also connected every viewport to the status bar, named
the source, and blanked the label when none is up. So this feature inherits a readout that is already
pointed at the right viewport, and only has to change what it is pointed at *with*.

**Per-viewport editor work is negligible; between-frame editor work is not.**
`RenderTargetWindow::DrawFrame` is not virtual and none of the three subclasses overrides it, so a
viewport's render-thread callback builds a `RenderJob` and calls `bgl` — the CPU cost of a viewport
is `bgl`'s recording cost to within a struct copy. But `Renderer::Post`/`Invoke` queue closures onto
the same thread's event loop, so they run between `Frame()` iterations, inside no target's frame and
invisible to anything measured per target. ADR-11 is why the editor measures its loop.

**The editor's smoothing is already what `bgl` should own.**
`RenderTargetWindow` holds `core::RollingWindow<120>` with a 30-frame emit interval and a
`>20 ms` missed-vblank threshold. ADR-4 moves the smoothing down; `core::RollingWindow`
(`libs/core/include/core/stats/RollingWindow.h`, `Push`/`Mean`/`Max`/`Size`/`Reset`) is the container,
and `bgl` already links `core`.

## What changes

| Area | Change | Risk |
|---|---|---|
| `libs/bgl/src/cmd/` | `ICommandList` gains a bracket for the timed region; `ICommandQueue` gains a non-blocking `TryGet…` keyed on a fence value | The seam must express both backends without leaking either; getting it wrong is a rewrite of both implementations |
| `libs/bgl/src/metal/cmd/` | Retain the committed `MTL::CommandBuffer` per in-flight fence; read `GPUStartTime`/`GPUEndTime` on completion | A retained buffer that is never released is a per-frame leak; the release must be driven by the fence |
| `libs/bgl/src/d3d12/cmd/` | Timestamp query heap, `EndQuery` pair, `ResolveQueryData` into a readback buffer, `GetTimestampFrequency` conversion | Mapping a buffer written this frame is the classic failure; GPU validation is the gate |
| `libs/bgl/include/bgl/` | `RenderTargetDesc` gains the opt-in flag; a `FrameStats` POD; `IRenderTarget::GetFrameStats` | Public API — the shape is what clients live with |
| `libs/bgl/src/gfx/RenderTargetBase.h` | Per-frame-slot timing record beside the existing fence | Must reset with the ring on resize, like the fences do |
| `libs/bgl/src/gfx/RenderContext.cpp` | `BeginFrame`/`EndFrame` stamp the CPU clock, bracket the fence wait and the present, bracket the list, harvest completed GPU times | The harvest must not block; a target with stats off must take no extra path; a blocking region left unbracketed silently lands in the CPU figure |
| `apps/editor/src/Windows/RenderTarget/` | `ReportFrameTiming`, `FrameStatsUpdated`, the rolling window and the missed counter are deleted (ADR-9); the window reads `GetFrameStats` | Deleting a signal `MainWindow` connects to — both sides move together |
| `apps/editor/src/Render/Renderer.cpp` | Times a `Frame` iteration and the gap between iterations (ADR-11) | The measurement must not itself become per-frame work worth measuring |
| `apps/editor/src/MainWindow.cpp`, `util/frame_stats_text.*` | The readout carries ADR-2's four figures instead of three, and drops the missed count | Its source and label are already right after #452 |
| `docs/rhi.md`, `docs/bgl_api.md` | The GPU-timing seam and the public stats accessor | — |

## Tasks

Bottom-up, `bgl` before the editor, RHI before the public API.

**1. RHI: GPU time for a submission.**
The seam from ADR-1, both backends. `ICommandList` brackets the timed region (D3D12 writes the two
timestamps; Metal is a no-op — the command buffer measures itself). `ICommandQueue` answers, without
blocking, for a fence value it previously returned from `ExecuteCommandList`. Nothing in `bgl` calls
it yet; this is deliberate dead scaffolding, and the tests are its only caller.
*Gate:* a `bgl_tests` case that submits a trivial command list and a deliberately heavy one, and
asserts the reported GPU duration is non-zero once each fence completes and larger for the heavy
one. Asserts the query is unavailable before the fence completes rather than returning a stale or
zero value. `--gpu-validation` clean.

**2. `bgl`: per-target CPU statistics and the public accessor.**
`RenderTargetDesc`'s opt-in flag, the `FrameStats` POD, `IRenderTarget::GetFrameStats`, the per-slot
record on `RenderTargetBase`, and the CPU-clock and cadence halves filled in by
`RenderContext::BeginFrame`/`EndFrame` — GPU field present and zero. The fence wait and the present
are bracketed here too, since ADR-10's `blocked` figure and the CPU figure are the same
measurement seen from both sides. The smoothing window moves down from the editor; the
missed-vblank counter is dropped per ADR-9. The ring's records reset with the fences on resize.
*Gate:* `bgl_tests` on a headless target — CPU time is non-zero, the completed-frame counter
advances once per frame even though a headless target presents nothing, and a target made
deliberately GPU-bound reports a `blocked` figure that grows while its CPU figure does not, which is
the assertion that proves ADR-10's subtraction actually happened; a target created without the flag reports statistics unavailable; a resize
clears the accumulated window rather than carrying a stale maximum across it.

**3. `bgl`: GPU time wired into the statistics.**
Task 1's seam feeds task 2's record. `EndFrame` brackets the list and stores the fence; a later
frame harvests whatever has completed, per ADR-5.
*Gate:* the acceptance case — after the ring's lag, GPU time is greater than zero, and a
many-instance frame reports a larger mean GPU time than a near-empty one. `--gpu-validation` clean.
`docs/rhi.md` and `docs/bgl_api.md` updated in the same commit.

**4. Editor: the readout reads `bgl`'s numbers, and the loop is timed.**
#452 already made the status bar follow the rendering viewport, so this task changes the source of
the figures, not who they are about. `RenderTargetWindow` loses `ReportFrameTiming`,
`FrameStatsUpdated`, its rolling window and its missed counter, and reads
`IRenderTarget::GetFrameStats` instead; `editor::FrameStatsText` grows the fields ADR-2 added and
loses the missed count per ADR-9. `Renderer` times a whole `Frame` iteration and the gap between
iterations per ADR-11, which is the one figure `bgl` cannot supply.
*Gate:* `just test editor` green, with `[framestats]` extended to the new fields, and a case pinning
that a closure posted to the render thread lands in the loop figure and in no viewport's — the ADR-11
blind spot, asserted rather than assumed. The visual result — the number changing with
the material preview's own content and not with another viewport opening — is confirmed by running
the editor; this checkout cannot capture the screen, so that half is the reviewer's.
