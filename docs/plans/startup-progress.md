# startup-progress — implementation plan

## Context

The editor shows nothing at all until `MainWindow::Build()` returns. `main.cpp` constructs the
window and only then calls `show()`, and `Build()` creates the `Renderer`, whose constructor calls
`bgl::CreateGraphics` — which builds every pipeline the renderer will ever use (the 18 `PsoType`
forward kernels plus the skybox, post-process, outline, TAA resolve, compact-instances,
skinned-pose, transparent-sort and BRDF-LUT passes) before returning. On a cold shader cache that is
tens of seconds of no window, no icon and no cursor feedback; `docs/shader_cache.md` opens by saying
compiling shaders dominates startup, and the cache exists precisely because it does.

The work is already off the UI thread — `Renderer` starts its `bgl-render` `QThread` first and runs
`CreateGraphics` inside `Invoke` — but `Renderer::AwaitClosure` spins on `QSemaphore::tryAcquire`
without pumping the event loop, so the GUI thread has no opportunity to paint anything while it
waits.

Opening a project then adds two modal questions before the editor is usable: `OfferTextureRefresh`
and `OfferProjectUpdate` each ask whether to do work that a project cannot draw without — loads
refuse a stale container rather than re-cooking one, so declining leaves the viewport unable to open
the assets. A question whose only sane answer is Yes is a dialog that exists to be dismissed.

Underneath, neither `AssetStore::Reimport` nor `AssetStore::Migrate` can say anything about what it
is doing: both take a `dryRun` bool, run to completion and hand back a report. The editor's screen
therefore reads `Rebuilding derived assets...` for the whole run, however many meshes, rigs, clips
and texture extracts that is.

## Decisions

- **ADR-1 — A splash screen goes up before `MainWindow` is constructed, and the startup work reports
  into it.** `main.cpp` shows a frameless progress widget, hands `MainWindow` a progress sink, and
  closes the splash when the window is ready to show. This is what Unreal's and Unity's editors do,
  and it is the only place a report about *building the window* can be displayed. *Rejected: showing
  an empty `MainWindow` first and covering the work with the existing modal
  `background::RunWithLoadingScreen`, because the renderer is built in the constructor — so either
  the constructor stays uncovered, or `Renderer` construction moves out of `Build()` and startup is
  reordered for a cosmetic reason.*

- **ADR-2 — The UI thread is freed by a nested event loop around the renderer's construction, not by
  making construction asynchronous.** A startup wait enters a `QEventLoop` and polls an atomic for
  completion while the render thread builds — the same poll-and-quit shape
  `background::RunWithLoadingScreen` already proved, for the same reason it was written that way.
  `Renderer::Invoke` and `AwaitClosure` are untouched, so nothing about the steady-state contract
  changes. *Rejected: a two-phase `Renderer` that returns half-built and completes later, because
  every existing caller then has to know about a state in which `GetGraphics()` is null; and pumping
  the event loop inside `AwaitClosure` generally, which would make every `Invoke` in the editor
  re-entrant.*

- **ADR-3 — Pipeline construction stays serial; only the reporting is new.** The render thread builds
  the pipelines one at a time as it does today and names each one as it starts. Startup costs what it
  costs now, but it stops looking hung. *Rejected: fanning the pass `Init()` calls across a worker
  pool, because `ShaderCache`, the `ID3D12PipelineLibrary` / `MTL::BinaryArchive` and
  `ResourceManager` have no synchronisation anywhere and Slang's global session is per-thread — that
  is a bgl threading change that deserves its own PR, and nothing in this one forecloses it.*

- **ADR-4 — bgl reports progress through one optional sink on `GraphicsOptions`, and declares its
  pipeline total before it builds any.** The knob sits beside `shaderCacheDir`, which is the
  precedent for "startup configuration that is not an RHI object" (`docs/shader_cache.md` § Design
  Choices). Declaring the total up front is what keeps the count and the reporting from drifting when
  a pass is added. *Rejected: an indeterminate busy indicator, which needs no total but cannot say how
  much is left; and a `bgl::I*` progress interface, which would make an internal startup detail part
  of the RHI.*

- **ADR-5 — assetlib gets exactly one progress seam, and the existing bare callback moves onto it.**
  `AssetStore::RefreshImportedTextures` already takes a `std::function<void(size_t, size_t)>`;
  `Reimport` and `Migrate` need to name the source and the phase as well. One
  `assetlib::ProgressSink` carrying source, phase and done/total replaces it, threaded through all
  three — a refactor commit ahead of the feature. *Rejected: a second, richer sink beside the
  existing one, because two ways for a cook to report progress in one library header is what the
  strict bar on `libs/` exists to refuse.*

- **ADR-6 — The rebuild runs its items concurrently within a stage, with a barrier between stages.**
  Both walks are already staged and both record why. `Reimport` runs `c_Order` — rigs, then meshes,
  then clips — because a mesh names the rig it binds and a clip set sweeps its boxes through the
  meshes standing on disk. `Migrate`'s re-save walk ranks the other way, meshes before rigs before
  clips, because a regenerated `.banim` re-measures against meshes that must already be current.
  Parallelism inside a stage preserves both orderings and still uses every core, since meshes are the
  bulk of the work. It is well-founded: `AssetStore` holds only a data root and a
  `shared_ptr<const IFileSystem>`, and `IFileSystem` already promises every method is safe to call
  concurrently on one instance. *Rejected: fully parallel across sources, which breaks both
  documented orderings; and staying serial, which is the one part of startup that can be made faster
  without touching bgl's invariants.*

- **ADR-7 — Neither rebuild is offered any more; both just run.** `OfferProjectUpdate` and
  `OfferTextureRefresh` stop asking and become steps behind the progress screen. Declining left the
  project unable to draw its own assets, so the question had one correct answer and cost a modal to
  give it. *Rejected: dropping only the derived-asset prompt and keeping the texture one on the
  grounds that a re-extract costs an import's worth of supercompression — the same argument applies
  to a re-cook, and one silent step is easier to reason about than one silent and one asked.*

- **ADR-8 — The same sink drives both screens.** At launch it drives the splash; on a later
  `File → Open Project` it drives the existing modal `background::RunWithLoadingScreen`. One rule
  about what a rebuild reports, two things that display it. *Rejected: detailed reporting at startup
  only, which makes one operation report two different amounts depending on how it was reached.*

## Non-goals

- Parallel shader/pipeline compilation, and the locks in `ShaderCache`, the pipeline library and
  `ResourceManager` it would need (ADR-3).
- Any change to what the shader cache stores, how it is keyed, or when it is invalidated.
- Cancelling startup. The splash reports; it offers no button, because there is nothing sensible to
  do with a half-built renderer.
- Making `bgl` thread-safe in any general sense. Its objects remain owned by the render thread.
- Changing what `Reimport` or `Migrate` decide to rebuild — only how they report it and how many
  threads run it.
- A progress bar for anything after the window is up (imports, bakes, thumbnails), which already has
  `background::RunWithLoadingScreen`.

## Acceptance

- `just run assetlib_tests -- "[reimport]"` — a parallel rebuild reports every source and every
  phase exactly once, and produces byte-identical output to the serial one over the same project.
- `just run assetlib_tests -- "[migrate]"` — the stage barriers still hold under concurrency: a
  clip is never produced before the meshes it sweeps are on disk.
- `just run bgl_tests -- "[shadercache]"` — the progress sink is called once per pipeline, and the
  total it was told up front equals the number of calls.
- `just run editor_tests -- "[startup]"` — the sink-to-label rule, lifted into a free function, turns
  a bgl report and an assetlib report into the strings the splash shows.
- `just test` green, and `just format --check` on every touched file.
- **Eyes**: delete `shadercache/` beside the binary, launch the editor, and watch the splash name
  each pipeline and then each `.bmesh` and phase, with no dialog asking anything, until the window
  appears.

## Commits

1. `docs(plans): plan the editor startup progress bar` — this file. Gate: it is the PR's first
   commit, so the boundaries precede the diff.

2. `refactor(assetlib): one progress sink for every long cook` — introduce `assetlib::ProgressSink`
   and `ProgressEvent` (`include/assetlib/progress.h`), thread it through
   `WriteTextures`/`RefreshImportedTextures` in place of `TextureProgressFn`, and update the two
   callers. No behaviour change. Gate: `just run assetlib_tests -- "[texture]"` and
   `just run editor_tests -- "~[render]"`.

3. `refactor(assetlib): one parallel-for behind every threaded cook` — lift the atomic-counter,
   `vector<thread>`, join idiom that `envmap_bake.cpp` spells out three times into
   `src/parallel_for.h`, and move those three onto it. No behaviour change. Gate:
   `just run assetlib_tests -- "[envmap]"`.

4. `feat(assetlib): a rebuild says which asset it is on, and runs a stage at a time` — `Reimport`
   and `Migrate` take a `ProgressSink`, report the subject and phase of each item, and run each
   stage's items across the pool from commit 3. Gate: `just run assetlib_tests -- "[reimport]"` and
   `just run assetlib_tests -- "[migrate]"`.

5. `feat(bgl): graphics reports each pipeline it builds` — `GraphicsOptions::onPipelineProgress`
   plus an internal reporter that every pass's `Init` steps, with the declared total asserted equal
   to the number of steps at the end of the `RenderContext` constructor. Gate:
   `just run bgl_tests -- "[shadercache]"`.

6. `feat(editor): the startup screen, and a task that reports into someone else's` — extract
   `background::RunWithLoadingScreen`'s worker-plus-nested-loop core so a task can report into a
   caller-owned sink, and add the frameless startup screen `main.cpp` stands up. Gate:
   `just run editor_tests -- "[startup]"`.

7. `feat(editor): show what startup is doing, and stop asking about the rebuild` — `main.cpp` shows
   the screen and hands `MainWindow` the sink; `Renderer` gains the wait policy that pumps the GUI
   event loop while the render thread builds; the project's rebuild reports through the same sink,
   and `OfferProjectUpdate` / `OfferTextureRefresh` become the unconditional `UpdateProject` and
   `RefreshTextures`. Gate: `just run editor_tests -- "~[render]"`, and the Eyes check above.

   *Landed as one commit rather than the two planned: dropping the prompts is a rewrite of the same
   two functions the sink is threaded through, and splitting it would have produced an intermediate
   state that exists only to be a commit.*

8. `docs: the startup screen and the progress seams` — `docs/shader_cache.md`,
   `docs/assetlib_api.md`, `docs/asset_containers.md` and `apps/editor/CLAUDE.md` where each now
   says something untrue. Gate: read back against the diff.

9. `test(editor): pin that startup actually reports, not just what it says` — *not planned.* The
   rebase onto master picked up #510, which made a headless `MainWindow` stand in a test; the sink
   `main.cpp` hands it is then reachable end to end, which pins the failure the label tests cannot
   see — the reports never arriving. Gate: `just run editor_tests -- "[startup]"`.
