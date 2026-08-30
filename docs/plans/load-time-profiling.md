# load-time-profiling — implementation plan

## Context

Nothing says where the editor's start-up time goes. Three separate load paths get a project's
derived containers off disk and onto the GPU, and none of them is measured:

| Path | Owner | Thread | Reports |
|---|---|---|---|
| Mount | `Project::Open`, called from `MainWindow.cpp:340` | GUI | nothing |
| Staleness scans ×3 | `GetStaleImportedTextureSources`, `GetStaleGeometry`, `Reimport(dryRun)` — `MainWindow.cpp:582,705,707` | Qt worker | one static label each |
| Rebuild | `AssetStore::Migrate` → `Reimport` — `migrate.cpp:197`, `reimport.cpp:227`, up to 4 threads | Qt worker + pool | `ProgressSink`, and a `ScopedStage` per regenerated container |
| Acquire | `AssetManager::Acquire*` — `AssetManager.cpp:219,307,374,438` | **render thread** | nothing |
| Thumbnails | `AssetThumbnailCache::LoadTask::run` — `AssetThumbnailCache.cpp:106` | private 2-thread pool | nothing |

The acquire path is the sharpest gap. It must run on the render thread
(`AssetManager.h:22-24`), and a bake-token miss regenerates a container — or bakes a `.bvat`, via
`EnsureVatBaked` (`vat_freshness.cpp:143`) — *inline* there, blocking every viewport, under one
static "Uploading materials and geometry..." label (`AnimationPreviewWindow.cpp:353`). On a cache
*hit*, `cache_io.h`'s `load<T>` reports nothing at all. Shader compilation, which
`apps/editor/CLAUDE.md` names as most of a cold start, is one unnamed, untimed step
(`MainWindow.cpp:131`).

`core::logging::ScopedStage` covers 7 assetlib cook sites and 1 editor site, and cannot close this:
it is flat, so it says how long one thing took and never where a total went; it has no notion of the
four thread pools above; and a stage placed before `CreateGraphics` has no file sink, because
`init_file_logger` runs from bgl's `Graphics` constructor. `docs/plans/pose-bounds-perf.md` is what
the flat shape costs — `groundClips` carried no stage line, so `bakePosedBounds` wore the blame for a
cost 22× larger than its own, for a year.

The output is split in two besides: `bgl.log` takes spdlog, `editor.log` takes Qt, two clocks and two
formats, so a start-up timeline straddles both.

## Decisions

- **ADR-1 — Vendor Tracy as the profiler rather than growing `ScopedStage` into one.** Unreal
  Insights (`TRACE_CPUPROFILER_EVENT_SCOPE`) and Unity's `ProfilerMarker` agree on the shape this
  needs: nested, named, thread-aware scopes read as a timeline. Tracy is that shape off the shelf —
  BSD-3-Clause, in vcpkg at 0.13.1, nanosecond zones, per-thread tracks, and a statistics view that
  answers "where did the 47 s go" directly. *Rejected: a thread-local scope stack and an indented
  roll-up in `libs/core`, which is writing a small and worse profiler, and would still print a tree
  per thread that a reader has to correlate by hand.*

- **ADR-2 — Tracy is on in every developer preset, debug and release, not gated behind a preset of
  its own.** The `enable_coverage` precedent (`docs/coverage.md`) is gated because instrumentation
  changes the performance of every subsystem it touches; a Tracy zone is a few nanoseconds on a path
  that already costs a file read and a GPU upload, so the reason to gate does not apply. Release
  especially: `pose-bounds-perf` measured release 27× cheaper than
  debug, so a debug-only profiler measures a cost the shipping build does not have. *Rejected: a
  `*-profiling` preset per platform, which doubles the preset matrix to buy back a cost that was
  never paid.*

- **ADR-3 — Refuse both of Tracy's tempting features: no `crash-handler`, and no `on-demand`.** The
  crash handler contends with `core::install_crash_handlers()` and cpptrace, which already own that
  seam and write the `<exe>_crash_<stamp>.log` the repo documents. `on-demand` is the subtler one and
  it is refused for the same reason this change exists: it collects only from the moment a viewer
  connects, and start-up is precisely the window nothing can connect before — the client listens and
  the viewer dials in, so a run profiled on demand is missing the part being measured. Buffering from
  process start is what makes a cold start capturable at all. The unbounded-memory risk on-demand
  normally answers does not arise here, because the non-goal below keeps zones off the frame loop: an
  editor sitting idle with no viewer attached emits no events. *Rejected: `default-features`, which
  takes the crash handler by omission; and `on-demand`, which would trade the measurement for a
  memory ceiling nothing is pressing against.*

- **ADR-4 — `ScopedStage` is retired, not kept beside Tracy.** Its 8 call sites become Tracy zones
  and the class, its test and its header go. Two ways to mark "this is a stage" is exactly the second
  path into a library the root `CLAUDE.md` forbids, and the one that would rot is the one without a
  viewer behind it. *Rejected: keeping it as a log-only channel. The stated cost of retiring it: in
  any build with Tracy compiled out, and for an `assetlib_cli` run read from its console output with
  no viewer attached, the per-stage timing lines `docs/gfx_debug.md` advertises are gone. ADR-2 is
  what makes that acceptable — there is no developer preset in which Tracy is off.*

- **ADR-5 — One log file, with Qt teed into spdlog.** The editor calls `init_file_logger` itself,
  before constructing the `Renderer`, and installs a Qt message handler that writes into the same
  sinks — so bgl's renderer lines, assetlib's messages and the editor's own `qInfo`/`qWarning`
  interleave in one file, in true order, on one clock, and a diagnostic emitted before the device
  exists is no longer written to a stdout a GUI launch does not have. *Rejected: leaving the two logs
  separate, which is the state this is meant to fix; and merging the other way into `editor.log`,
  which leaves `assetlib_cli` — which has no Qt — needing a second shape.*

- **ADR-8 — Vendor Tracy's `cli-tools`, and not its `gui-tools`.** A capture has to become a number
  somebody else can check: `tracy-capture` records a run headlessly and `tracy-csvexport` turns it
  into per-zone totals, which is how ADR-7's before/after table is produced and how a reviewer — or a
  later change — reproduces it without installing anything by hand. `gui-tools` is the timeline
  viewer and is not vendored: it drags glfw3, freetype, curl, imgui and a dozen more into the
  manifest for something every developer installs once, out of tree, and never builds from here.
  *Rejected: no tools at all, which was the first draft of this plan and left the acceptance
  criterion with no instrument behind it; and `gui-tools`, which pays a large configure cost on every
  machine for a binary that is not part of the build.*

- **ADR-6 — Instrument all five rows of the table above, including the two that are not start-up.**
  The acquire path and the thumbnail pool run whenever the editor is *used*, and both decode the same
  derived containers; measuring only the start-up spine would attribute a cold start and leave "why
  is opening the Animation panel slow" unanswerable. *Rejected: the start-up spine alone.*

- **ADR-7 — What the optimisation half fixes is chosen by the measurement, not here.** The
  instrumentation lands first, a cold and a warm start on this checkout's `test-project` are measured
  and reported, and the fix commits follow from what that indicts. Naming a target now — the three
  per-launch data-root walks are the standing suspect — would be choosing before the instrument that
  this change exists to build has said anything. *Rejected: committing to the scans up front, which
  risks spending the change on 400 ms of a 47 s start.*

## Non-goals

- **GPU per-pass timestamps and the on-screen frame breakdown.** `ROADMAP.md` § Profiling holds
  these as a FrameGraph feature; they are about a frame, not a load, and share nothing with this but
  the word.
- **Per-frame CPU profiling of the render loop.** Zones land where a load happens. `AssetThumbnailCache::Advance`'s
  slow-tick warning (`AssetThumbnailCache.cpp:473`) stays as it is.
- **Vendoring Tracy's GUI.** ADR-8: the timeline viewer is installed once per machine, out of tree.
- **Animation compression and texture transcode.** `docs/specs/animation_compression.md`, and the
  transcode that `pose-bounds-perf` measured as the majority of a full `migrate` wall clock. Both
  stay where they are.
- **Changing what a container holds, or the bake-token discipline.** `docs/asset_containers.md`
  stands; a miss still regenerates and never converts.

## Acceptance

- A before/after split of a **cold** and a **warm** editor start on this checkout's `test-project`,
  in the PR body, produced by the instrument this change adds.
- A Tracy capture of a start-up opens in the viewer showing nested zones across the GUI thread, the
  Qt worker, `Reimport`'s pool, the thumbnail pool and the render thread.
- `just test` green, with `libs/core/tests/src/ScopedStage_test.cpp` removed alongside the class it
  pins (ADR-4).
- A new `editor_tests` case: a message written through Qt and one written through spdlog both land in
  the single log file (ADR-5). It fails on today's two-file split.
- Whatever ADR-7's measurement indicts is pinned in the shape the repo already uses — a `[perf]` case
  asserting a read count or a ratio, never a wall-clock ceiling — where the fix has that shape.

## Commits

1. `docs(plans): plan a profiler that can see a cold start` — this file.
2. `build: vendor Tracy, enabled in every preset` — the manifest entry with `default-features`
   off and `crash-handler`/`on-demand` refused (ADR-2, ADR-3), and `core` linking
   `Tracy::TracyClient` publicly so every target downstream of it can open a zone.
   Gate: `just build` configures and links on `macos-clang-metal-debug`.
3. `refactor(core): retire ScopedStage for Tracy zones` — the 8 call sites convert, the class, its
   header and `ScopedStage_test.cpp` go, and `docs/gfx_debug.md` stops advertising a channel that no
   longer exists (ADR-4). Gate: `just test core assetlib`.
4. `feat(editor): one log, not two` — `init_file_logger` before the `Renderer`, and Qt's message
   handler into the same sinks (ADR-5). Gate: the new `editor_tests` case in Acceptance.
5. `feat(assetlib): the container decode says what it cost` — zones through `cache_io`'s `load<T>`
   and the regen path, so a cache hit is distinguishable from a miss in the capture.
   Gate: a capture of `assetlib_cli` shows both.
6. `feat(gamelib): the acquire path says what it cost` — `AssetManager::Acquire*` and
   `EnsureVatBaked`, the work that runs inline on the render thread (ADR-6).
   Gate: a capture shows an acquire nested under the render thread's track.
7. `feat(editor): the start-up spine says what it cost` — mount, the three scans, the shader
   compile, `Migrate`, the explorer root and the thumbnail pool.
   Gate: a capture of a cold start shows the tree across all five threads.
8. **Measure**, and report the cold and warm split before writing anything further (ADR-7).
9. `perf(<layer>): …` — chosen by 8, with the `[perf]` case that pins it where it has that shape.
