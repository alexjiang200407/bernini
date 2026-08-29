# headless_render_target_window — implementation plan

## Context

`RenderTargetWindow`'s constructor is unconditionally window-backed: it reads `winId()` into
`RenderTargetDesc::wnd`, sets `headless = false`, and dereferences `m_Desc.renderer` with no check
([RenderTargetWindow.cpp](../../apps/editor/src/Windows/RenderTarget/RenderTargetWindow.cpp),
`:96-106`). A test cannot realise a native window, so every class that owns one is uncovered:
`RenderTargetWindow` itself, `LevelEditorWindow`, `MaterialPreviewWindow`,
`AnimationPreviewWindow`, and `MainWindow`, whose constructor creates the device and builds the
three viewports. [apps/editor/CLAUDE.md](../../apps/editor/CLAUDE.md) already names them and names
the fix: *"Covering them needs a seam first: a `headless` flag on `RenderTargetWindowDesc`."*

The device is not the obstacle. `editor_tests` links `bgl_d3d12_agility` and may call
`CreateGraphics`; `AssetThumbnailCache` is covered end to end because it owns a *headless* target
and never asks for a `winId()`. `headless` is already a first-class bgl concept — `wnd` is
documented "Ignored when headless" ([IRenderTarget.h](../../libs/bgl/include/bgl/IRenderTarget.h)
`:30`) and a headless target "presents nothing and advances round-robin"
([RenderTargetBase.h](../../libs/bgl/src/gfx/RenderTargetBase.h) `:238`). The only thing missing is
a way to ask a viewport for it.

Two things the seam turned out to need that a `headless` flag alone does not give:

- `MainWindow::Build` reads `config.json` from `get_executable_path().parent_path()`, and
  `editor_tests` runs from the same `CMAKE_RUNTIME_OUTPUT_DIRECTORY` that
  `apps/editor/CMakeLists.txt:174` deploys that file into. A test cannot put `headless` in it
  without writing the shipping editor's config, and inherits its `startupProject` besides.
- `startupProject` is the *only* non-modal door into `SetActiveProject`: `NewProject` and
  `OpenProject` both open a `QFileDialog`, and `OpenProjectAt` is private.

## Decisions

- **ADR-1 — a flag on the desc, not a subclass.** The window-backed and headless paths differ in
  three assignments. *Rejected: a `HeadlessRenderTargetWindow`, because the thing under test must
  be the class that ships, and a subclass would fork every viewport type in two.*
- **ADR-2 — headless still takes a real device.** `AssetThumbnailCache_test` already renders this
  way, so the covered path is the shipping one. *Rejected: a fake `IGraphics`, for the reason
  [apps/editor/CLAUDE.md](../../apps/editor/CLAUDE.md) gives — `MaterialEditorWindow`,
  `AssetThumbnailCache` and `TextureNode` each degrade on a null `Renderer` and no shipping path
  produces one, so a fake buys coverage of three branches no user reaches.*
- **ADR-3 — an explicit extent rather than the widget's.** An unshown widget's `width()`/`height()`
  are its layout default, so a target sized from them is not the target a person sees. *Rejected:
  `sizeHint()`, which is the same guess with more steps.*
- **ADR-4 — a null `Renderer` asserts.** No shipping path produces one, and the desc is filled in by
  `MainWindow::Build` immediately after it constructs the `Renderer`. An assert states the
  precondition and adds no branch. *Rejected: constructing inert, because `SetCamera`, `SetTime`,
  `DrawFrame` and `UpdateViewport` all dereference unconditionally and would each need a guard
  nothing reaches.*
- **ADR-5 — `MainWindow` takes the path to its `config.json`.** The headless choice is a config key
  like every other viewport setting, and the test writes a config of its own in a temp directory —
  which also gives it `startupProject`, the only non-modal route into `SetActiveProject`.
  *Rejected: a `bool headless` constructor parameter, because it leaves `Build` reading the
  deployed file, so the test would still inherit the developer's `startupProject` and could not
  open a project of its own.* *Rejected: a `headless` key in the deployed `config.json`, because
  `editor` and `editor_tests` share that file.*
- **ADR-6 — `editor::IFollowsProject`, found by a walk.** `SetDataRoot` is an ad-hoc convention on
  two panels, called by hand in `SetActiveProject`. `editor::IHoldsAssets` exists because the
  hand-written list that came before it "was short by four"; this is the same failure in the same
  window. *Rejected: asserting the two concrete panels in the test, because it pins today's panels
  and leaves the next one's omission silent — which is the whole point of the exercise.*

## Non-goals

- **Modal dialogs stay untestable.** `QFileDialog`, `QMessageBox` and `QMenu::exec` are called on
  concrete Qt types with no injection seam. `NewProject`, `OpenProject`, `CleanUnusedTextures` and
  the offers inside `SetActiveProject` are reached only where an empty project takes their early
  return.
- **No golden image.** This buys construction and lifetime coverage — that a panel is released in
  the right order and follows the project's data root. What a viewport *draws* stays `bgl_tests`' job.
- **No `Drop` synthesis.** Unchanged by this.
- **`ContentExplorerWindow` does not become an `IFollowsProject`.** Its `SetRootPath` is a view root
  with an ordering constraint against the thumbnails, not a data root; folding it into the walk
  would put that ordering at the mercy of `findChildren`.

## Acceptance

- `[render]`-tagged cases in `editor_tests` that build a `MainWindow` against a headless config in a
  temp directory, and assert:
  1. every `RenderTargetWindow` is destroyed before the `Renderer`;
  2. every `IFollowsProject` in the tree saw the new data root when a project opens;
  3. every `RenderTargetWindow` under a headless `MainWindow` is itself headless, and the count is
     the panels built — so a panel added later that forgets either is a red test rather than a
     silent omission.
- `just test editor` green, and `just run editor_tests -- "~[render]"` still about a second.

## Commits

1. `docs(plans): plan a headless viewport and the seams it needs` — this file.
2. `feat(editor): a viewport can be asked to stand without a window` — `headless`,
   `headlessWidth`/`headlessHeight` on `RenderTargetWindowDesc`, the constructor branch, the
   `IsHeadless()` accessor and ADR-4's assert; threaded through `MaterialEditorWindowDesc` and
   `AnimationEditorWindowDesc`. Gate: `just build editor_tests`.
3. `feat(editor): a panel that follows the project says so` — `editor::IFollowsProject` and
   `editor::SetProjectDataRoot`, implemented by `MaterialEditorWindow` and
   `AnimationEditorWindow`, replacing the two hand-written calls in `SetActiveProject`.
   Gate: `just run editor_tests -- "[project]"`.
4. `feat(editor): MainWindow reads the config it is given` — the config-path parameter (ADR-5).
   Gate: `just run editor_tests -- "[mainwindow]"`.
5. `test(editor): a headless MainWindow pins its viewports' lifetime and data root` — the three
   cases above. Gate: `just run editor_tests -- "[mainwindow]"`.
