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

  **It is still an option — `BERNINI_PROFILING`, default ON — and the reason is not CPU cost.**
  Tracy's client opens a TCP socket and announces itself over UDP from process start; that is the
  mechanism, not a side effect, and it is not something a shipping binary should carry. `core` links
  it and everything links `core`, so the escape hatch is cheaper to add now than once the tree has
  more consumers — `ROADMAP.md` ships the game cross-platform, Xbox included. Off is headers-only and
  no call site changes.*

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
  viewer behind it. *Rejected: keeping it as a log-only channel. Two costs, both stated rather than
  waved at. First, in any build with Tracy compiled out, and for an `assetlib_cli` run read from its
  console with no viewer attached, the per-stage timing lines `docs/gfx_debug.md` advertised are
  gone; ADR-2 is what makes that acceptable, since there is no developer preset in which Tracy is
  off. Second: a stage line was **assertable** and a Tracy zone is not — Tracy's client
  streams to a server and exposes no in-process query — so `assetlib_tests`' "The posed-bounds bake
  says what it cost, and on what" is deleted with `CapturedLog.h`, and nothing here replaces it.
  What that case actually pinned is worth stating exactly, because it is smaller than it sounds: on
  a two-vertex, one-bone, three-frame fixture it asserted the line read "1 bones", "1 entries",
  "3 frames" and " ms". It was a smoke test that a stage names its dimensions at all — not a scaling
  guard, and it could not have caught a bake going superlinear by itself. The cases that do guard
  that shape are `Perf_test.cpp`'s `[perf][bounds]` pair, and this change does not touch them.*

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

- **ADR-9 — The re-parse `Reimport` pays is real, was fixed, was measured, and is not shipped.**
  ADR-7's measurement indicted it: `Reimport` cooks a stage at a time and re-parses each source per
  output kind, so three sources parse **seven** times — 10.1 s of a 24.2 s project rebuild. A bounded
  cache holding each parsed source across the stages that need it was built and did exactly what it
  was designed to do: 7 parses became 3, and parse time fell from 10.2 s to 4.7 s.

  It is not kept, because the saving does not survive the stages after it. Measured against a
  same-session control on the same binary and machine state (n=2 against n=1):

  | | cache off | cache on | |
  |---|---:|---:|---|
  | `assetlib glTF parse` | 10.2 s / 7 | 4.7 s / 3 | −5.5 s |
  | `assetlib clip floors` | 9.3 s | 10.4 s | +1.1 s |
  | `assetlib posed bounds` | 3.7 s | 5.4 s | +1.7 s |
  | `assetlib reimport` | 23.2 s | 21.4 s | −1.8 s |
  | `editor startup` | 24.6 s | 24.4 s | **−0.2 s** |

  Holding three parsed sources resident makes the memory-heavy skinning sweeps that follow slower,
  and 2.8 s of the 5.5 s comes straight back. That is precisely the cost `reimport.cpp`'s own comment
  predicts, and at the whole-start-up level the change is indistinguishable from run-to-run spread —
  so it would be added complexity and resident memory bought with nothing a user can feel.
  *Rejected therefore: shipping it on the strength of its −7.7% on the rebuild sub-phase alone; and
  tuning the cap until the regression shrinks, which is fitting a constant to one project.*

  What this does settle is the question `pose-bounds-perf.md`'s ADR-2 left open. Its withdrawal asked
  for a fresh design and a fresh measurement against #515's parallel stages; both now exist, and the
  answer is that **the re-parse is not where the win is**. Two further things that measurement
  ruled out, for whoever looks next: the source-major restructure ADR-2 originally proposed cannot
  work at all — `Reimport_test.cpp`'s "Two sources rebuild together" pins a clip set reading *another*
  source's `.bskel` from disk, so the stage barriers must stand — and the real target is elsewhere:
  `clip floors` plus `posed bounds` is 13 s of a 23 s rebuild, larger than the parse ever was, which
  is the sweep fusion `pose-bounds-perf` already names as the next thing.

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
- ADR-9's optimisation is measured against a same-session control, not against numbers taken before
  the build changed. It ships only if the whole start-up moves; it did not, so no `[perf]` case for it
  lands either — a case pinning a shape nothing ships would pin a behaviour that is not there.

## Commits

1. `docs(plans): plan a profiler that can see a cold start` — this file.
2. `build: vendor Tracy, enabled in every preset` — ADR-1, ADR-2, ADR-3, ADR-8.
   Gate: configures and links on `macos-clang-metal-debug`.
3. `refactor(core): retire ScopedStage for Tracy zones` — ADR-4. Gate: `just test core assetlib`.
4. `feat(editor): one log, not two` — ADR-5. Gate: the four `[log]` cases in `editor_tests`.
5. `feat(assetlib): the container reads say what they cost` — ADR-6, the read funnels and the
   `AssetStore` doors a start-up knocks on.
6. `feat(gamelib): the acquire path says what it cost` — ADR-6, the render thread's half.
7. `feat(editor): the start-up spine says what it cost` — ADR-6, the rest.
8. `feat(assetlib): the rebuild's own phases, and what they measured` — the first capture left 110 s
   of a 133.9 s cold start in no zone at all; this closes that and records the four scenarios.
9. `docs(plans): the re-parse is not where the win is` — ADR-9, and ADR-7 discharged.
10. `refactor(assetlib): tell the two VAT zones apart`.
11. `build: give profiling an off switch, and name the pools` — the network hatch ADR-2 now argues
    for, plus the two defects a `BERNINI_PROFILING=OFF` build turned up.
12. `docs(plans): say what actually landed` — this list, reconciled against the log.

Gate for the whole: `just test` green under **both** `BERNINI_PROFILING=ON` and `OFF`.
