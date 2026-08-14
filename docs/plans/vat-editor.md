# feat/vat-editor — an Animation panel that previews a rig's VAT clips

## Context

VAT landed on master (#340) with no editor surface. `apps/editor` contains no VAT code at all,
`RenderTargetWindow::DrawFrame` never sets `RenderJob::time`
(`apps/editor/src/Windows/RenderTarget/RenderTargetWindow.cpp:126`), so the editor's clock is
permanently zero, and verifying a bake today means writing a bespoke app. Skinned preview is coming
later (the whole skinned tier in `ROADMAP.md` is unstarted) and should reuse this viewer's shell
rather than get a second one.

## Decisions

- **ADR-1 — the viewer opens on the animated mesh.** A rigged `.bmesh` is dropped or opened; it
  names its rig by path (`BMesh::skeleton`), and every `.banim` records the same path and a
  signature (`assetlib_structs/Animation.h:46-47`), so its clips resolve by comparing those over
  `Animations/` — no hashing, no ambiguity. *Rejected:* opening `.bvat` directly — it is a
  git-ignored build product (`docs/vat.md`), not an asset; *rejected:* opening the `.banim` — a
  clip names no mesh, so the reverse lookup would have to be invented; *rejected:* reusing
  `ContentExplorerWindow::FindMatchingSkeleton` — it exists for the import case that holds no path,
  only an in-memory skeleton, and it is a member of a Qt window class.
- **ADR-2 — a fourth dock panel.** An Animation panel tabified with the Level and Material editors,
  embedding its own `RenderTargetWindow` preview over the shared scene — the `MaterialEditorWindow`
  pattern, and `DriveViewportsFromTab` already parks hidden viewports. *Rejected:* floating windows
  (no precedent, one swapchain each); extending `MaterialPreviewWindow` (couples unrelated editors).
- **ADR-3 — the panel owns its clock.** It supplies `RenderJob::time`; play, pause, scrub, step and
  speed are all window policy over that float, which is `docs/vat.md`'s design. *Rejected:* a
  scrub/pause API on `ISceneView` — the engine deliberately has none.
- **ADR-4 — full transport in this feature.** Clip list, play/pause, scrubbable timeline, frame
  step, playback speed, metadata readout (duration, sample rate, frame count, loop flag).
  *Rejected:* a play-only first cut with transport later — a viewer that cannot hold a named frame
  verifies nothing about a bake.
- **ADR-5 — the model speaks source-asset concepts** (mesh, skeleton, clips), never VAT rows, so a
  future skinned backend slots in behind the same panel. *Rejected:* modelling on the `.bvat`
  layout — it ties the panel to the one bake format the skinned path will not share.
- **ADR-6 — a stale or missing `.bvat` is baked off the UI and render threads** under
  `background::RunWithLoadingScreen` (`apps/editor/src/Async/BackgroundTask.h:102`), the facility
  `MaterialPreviewWindow` already uses for an off-thread assetlib cook
  (`MaterialPreviewWindow.cpp:216`). The exists→stale→bake→save step itself is exposed by gamelib
  (task 1) — pure assetlib, no bgl — and `AcquireVatMesh` afterwards finds the file fresh and only
  uploads. *Rejected:* letting `AcquireVatMesh` bake inside a `Renderer::Invoke` — a bake is
  seconds of CPU skinning, and a multi-second stall of every viewport reads as a hang; *rejected:*
  a panel-side copy of the freshness pipeline — two homes for one rule.
- **ADR-7 — the preview instance is always `{clip, phase 0, rate 1}`**, recreated only on clip
  switch; speed and scrubbing never touch the instance. *Rejected:* recreating per speed/scrub
  change — the clock already expresses it.
- **ADR-8 — a rig with several matching `.banim` files gets a source dropdown**; switching releases
  the preview geom to zero (which evicts `AssetManager`'s cache entry) and re-acquires with the new
  animations path, relying on ADR-9 for the rebake. *Rejected:* widening gamelib's geom cache key
  by the animations path — two live geoms would then contend for the one extension-swapped `.bvat`
  path.
- **ADR-9 — gamelib treats a `.bvat` baked from a different animations file as stale.** Today
  `vatIsStale` compares only the stamps recorded in the container
  (`libs/assetlib/src/vat_bake.cpp:279-284`) and `AcquireVatMesh` reads `animationsRelPath` only
  inside its bake branch — so an acquire naming a different `.banim` silently previews the old
  clips. The defect is gamelib's, and the "editor viewport playback" follow-up is its next caller.
  *Rejected:* the panel deleting the `.bvat` before re-acquiring — a workaround one layer above the
  rule.

## Non-goals

- No level-viewport placement or playback of VAT instances, and no fix for the outline mask drawing
  VAT through the static kernel — that is the "editor viewport playback" follow-up.
- No blend authoring, and no clip editing of any kind.
- No skinned runtime work; `GeomType::kSkinnedMesh` stays commented out.
- No change to `.bvat`'s build-product status, and no `.bvat` thumbnails or import category.
- No double-click open routing in the Content Explorer; the panel is fed by drag-drop and an Open
  button, like the Material Editor.
- No headless seam for `RenderTargetWindowDesc`; the viewport family stays manual-only for now.

## Acceptance

`gamelib_tests` pin the freshness rule (a re-acquire naming a different `.banim` rebakes and
returns that file's clips). `editor_tests` pin the GUI-free logic: mesh → `.banim` binding
resolution against fixture assets, and the transport's time/frame arithmetic including loop wrap,
non-loop clamp, frame step and speed. The rendered result is verified manually in the running
editor against the coyote: play, pause, scrub to a named frame, step one frame, change speed,
switch clip, and confirm a hidden Animation tab stops rendering.

## What the survey found

Editor shell (`apps/editor`):

- Qt 6 Widgets; three hardcoded `QDockWidget`s built in `MainWindow::Build`
  (`apps/editor/src/MainWindow.cpp:134-211`); the Material dock is tabified onto the Level dock.
  Adding a panel means editing `MainWindow::Build` and `MainWindow.h` — there is no registry.
- The asset-viewer pattern is a `RenderTargetWindow` subclass
  (`apps/editor/src/Windows/RenderTarget/RenderTargetWindow.h:31`): own `RenderTargetRef` +
  `SceneViewRef` over the one shared `IScene` owned by `Renderer`
  (`apps/editor/src/Render/Renderer.h:120`). Every bgl call must run on the render thread inside
  `Renderer::Post`/`Invoke`. `MaterialPreviewWindow` is the worked example: orbit camera, drag-drop
  of a `.bmesh`, its own environment.
- `background::RunWithLoadingScreen` (`apps/editor/src/Async/BackgroundTask.h:102`) is the
  off-UI-thread facility with modal progress and cooperative cancel;
  `MaterialPreviewWindow.cpp:216` already runs an assetlib cook under it.
- Window logic is testable without a window: `MaterialPreviewWindow_test.cpp` pins
  `GetInstanceTargets` as a pure function — the precedent for testing the new windows' seams.
- `MainWindow::DriveViewportsFromTab` (`MainWindow.cpp:535`) gates rendering on dock-tab
  visibility; a new tabified viewport must join it.
- `MainWindow` owns the one `game::AssetManager` (`MainWindow.h:111`), shared with the thumbnail
  cache; it is reset inside a `Renderer::Invoke` on project close and shutdown
  (`MainWindow.cpp:332,502`) — anything holding acquired refs must release first.
- There is no open-asset routing: double-click opens folders only
  (`ContentExplorerWindow.cpp:323`); the explorer's views are drag-only.
- Import already writes the rig: `.bskel` under `Skeletons/`, `.banim` under `Animations/`. A
  rigged `.bmesh` names its skeleton by data-root-relative path (`BMesh::skeleton`), and each
  `.banim` records that path plus a signature (`Animation.h:46-47`). A rig may have several
  `.banim` files — one per exported clip is a supported import shape.

VAT pipeline (`libs`):

- One `.bvat` per rig holds all clips of one animations file
  (`libs/assetlib_structs/include/assetlib_structs/BVat.h:59`). Clip metadata reaches the app as
  `AssetManager::VatClipInfo { name, frameCount, sampleRate, duration, loop }`
  (`libs/gamelib/include/gamelib/AssetManager.h:141`); bgl never sees names or durations, so the
  panel holds the table itself.
- `AcquireVatMesh(relPath, animationsRelPath, meshIndex)` bakes on demand when the `.bvat` is
  missing or stale, then uploads and stands the geom up
  (`libs/gamelib/src/AssetManager.cpp:329-455`). `vatIsStale` compares only the three stamps
  recorded in the container (`vat_bake.cpp:279-284`) — the requested animations path never enters
  the check (ADR-9). The geom cache key is `"{relPath}#{meshIndex}#vat"`
  (`AssetManager.cpp:336`), and the `.bvat` path is the mesh path with the extension swapped.
  Geoms are refcounted; release to zero destroys and evicts (`AssetManager.h:280-284`).
- Per-instance state is `VatInstanceDesc { clip, phase, rate }`, written once by
  `CreateVatMeshInstance` (`libs/bgl/include/bgl/ISceneView.h:50-67`); the shader derives the frame
  from `phase + time * rate * sampleRate`, wrapping loops and clamping the rest
  (`libs/bgl/shaders/src/Forward_VatMesh.slang:31-47`). `rate = 0` freezes on `phase`. There is no
  mutate-instance API; change of clip is destroy + recreate.
- The clock is `RenderJob::time`, one float supplied per `DrawFrame` per view
  (`libs/bgl/include/bgl/RenderJob.h:15`) — so a window that owns its own time owns its transport.
- The bake is pure assetlib (`bakeVat`/`saveVat`, `libs/assetlib/include/assetlib/vat_bake.h`), so
  it can run on a worker thread with no bgl involvement.
- There is no skinned runtime path; VAT is the only way animation reaches the screen.

## What changes

- `libs/gamelib` — `AcquireVatMesh`'s exists→stale→bake→save step becomes a callable gamelib
  exposes (pure assetlib, safe off the render thread), and staleness additionally compares the
  requested animations path against the one the `.bvat` records (ADR-9). `gamelib_tests` grows the
  matching cases.
- `apps/editor/src/Windows/AnimationEditor/` (new) — the panel (`AnimationEditorWindow`), the
  preview viewport (`AnimationPreviewWindow`), and the GUI-free model files beside them.
- `apps/editor/src/MainWindow.{h,cpp}` — fourth dock member, `Build` wiring, tabify,
  `DriveViewportsFromTab`, `AssetManager*` handoff, shutdown release ordering.
- `apps/editor/tests/` — new suites for binding resolution, transport arithmetic, and the new
  windows' pure seams.
- Possibly a small shared home for the orbit camera if lifting `MaterialPreviewWindow`'s is cheaper
  than a second copy — decided in task 3, not here.

What could break: shutdown and project-close ordering (the panel's acquired refs must drop before
`m_Assets.reset()` runs on the render thread); `DriveViewportsFromTab` with a third tabified
viewport; the thumbnail cache sharing the `AssetManager` while the panel acquires; existing
`.bvat`s in projects predating ADR-9's stricter staleness (they rebake once, seconds each).

## Tasks

1. **gamelib: VAT freshness owns the animations path.**
   Extract `AcquireVatMesh`'s exists→stale→bake→save step into a function gamelib exposes, and
   make it treat a `.bvat` whose recorded animations path differs from the requested one as stale
   (ADR-9). `AcquireVatMesh` calls the same function, so the rule has one home.
   *Gate:* `gamelib_tests` — acquire baked from `.banim` A, release to zero, re-acquire naming B:
   the clip table is B's; plus the helper pinned directly on the missing/fresh/stale-by-stamp/
   stale-by-path shapes.

2. **Animation binding + playback transport, GUI-free with tests.**
   `AnimationBindings`: a rigged `.bmesh` in, its skeleton path and the matching `.banim`
   candidates out, by the path/signature compare of ADR-1. `PlaybackTransport`: the clip table in,
   `{activeClip, jobTime}` out; play/pause/scrub/step/speed, loop wrap and non-loop clamp as pure
   time arithmetic. No Qt types in either.
   *Gate:* new `editor_tests` cases — binding resolved against synthesized rig assets in a temp
   root (the `ImportedRig_test` pattern), including the several-`.banim` and no-match shapes;
   transport math pinned frame-exactly — green in `just test editor`.

3. **Animation panel + preview viewport.**
   `AnimationEditorWindow` as the fourth tabified dock; `AnimationPreviewWindow` subclassing
   `RenderTargetWindow` with its own view/target and an orbit camera; accepts a rigged `.bmesh` by
   drag-drop and an Open button and shows it as static geometry (bind pose) — which stays the
   fallback for a rig with no matching clips. No transport UI yet.
   *Gate:* the new windows' pure seams pinned in `editor_tests` (the `MaterialPreviewWindow_test`
   pattern — at minimum the drop-accept filter and the acquire/release bookkeeping); manual — drop
   the coyote mesh, orbit it, confirm the hidden tab stops rendering and project close/reopen does
   not assert.

4. **VAT playback + transport UI.**
   On drop with resolved clips: task 1's bake step under `RunWithLoadingScreen` when stale,
   `AcquireVatMesh`, `CreateVatInstance{clip, 0, 1}`; the panel feeds `RenderJob::time` from
   `PlaybackTransport`. Clip list, timeline, play/pause, frame step, speed, metadata readout; the
   `.banim` source dropdown with release-to-zero + re-acquire on switch; release ordering on close.
   *Gate:* transport-glue cases in `editor_tests`; manual coyote script — play, pause, scrub to a
   named frame, step, 0.25×, non-loop clamp at the last frame, clip switch, `.banim` switch.

Landing (§ 5 of the feature): whatever outlives this — the panel's existence and its clock policy —
moves into `docs/vat.md` as a short editor section, and `ROADMAP.md` gains a checked editor-preview
line under Vertex Animation Textures; this plan is then deleted by the feature's last PR.
