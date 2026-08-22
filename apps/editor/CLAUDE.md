# editor

editor is the Bernini game editor: a desktop application for authoring scenes and
managing resources. It is also the offline asset-cook host — artists export glTF, the
editor imports it (via assetlib) and converts it into the game-ready format.

- CMake targets: `editor_lib` (everything), `editor` (just `main.cpp`), `editor_tests`.
  Built **automatically only when Qt6 is found** — the root `CMakeLists.txt` probes
  `find_package(Qt6 ...)`; there is no manual `BUILD_EDITOR` flag.
- Builds on Windows (D3D12) and macOS (Metal). macOS needs Qt on `CMAKE_PREFIX_PATH`.
- CMake: `./CMakeLists.txt`
- Links `gamelib` as well as `bgl` and `assetlib`. `gamelib` is the seam that owns "load this
  asset into a scene", and its `AssetManager` holds the **only** implementation of the
  baked-vs-loose branch that turns an `assetlib::BMaterial` into a `bgl::MaterialHandle`.
  Reach for it rather than rebuilding that branch — a material must render the same however
  it was loaded.

## The bar here is the loose one

The editor is a frontend, and awkward local organisation is tolerated in it: nothing links this
target, so a widget that grew badly costs whoever edits that widget and nobody else. The libraries
underneath do not get that licence — see [the root CLAUDE.md](../../CLAUDE.md) § The bar each
subsystem is held to.

Two things the licence does **not** cover, because both leak out of the app:

- [STYLE.md](../../STYLE.md) still applies in full, and so does the layering rule.
- **Never work around a library's shape from here.** When the editor has to restate something
  `assetlib` or `bgl` already owns — join a data root to a key by hand, re-derive a naming
  convention, branch on a case the library should be answering — that is a seam to fix down there,
  not a helper to add up here. The editor is the biggest client of both, so a workaround written
  here is the reason the library's shape never gets fixed.

## config.json

`config.json` (git-ignored, one per checkout, deployed next to the binary and read once by
`MainWindow::Build`) is machine-local: `startupProject` names the project to open on launch, and
`instanceName` names *this* editor. An `instanceName` leads the window title —
`A — Bernini Editor — Test Project` — so two editors run side by side for an A/B comparison can be
told apart where every other part of the title is identical. Empty, and the title is what it always
was. `config.example.json` carries both keys blank.

## editor_lib

Every editor source **except `main.cpp`** lives in `editor_lib`, an OBJECT library that
`editor` and `editor_tests` both link. `main.cpp` is held out because it owns `main()`,
and the test runner has its own — so the tests exercise the objects that ship rather
than a recompiled copy free to drift from them.

An OBJECT library rather than a STATIC one: Qt classes are reached through the
meta-object system as often as through the linker, and a static archive is free to drop
an object whose symbols look unreferenced, taking its moc registration with it.

`editor_lib` publishes its AUTOUIC output directory, because the editor's *headers*
`#include "ui_<Name>.h"` — so anything that includes an editor header needs the
generated `ui_*.h` on its include path.

# UI: Qt Widgets, not QML

The UI is **Qt Widgets**, authored by drag-and-drop in **Qt Designer** (`.ui` files).
QML/Qt Quick was deliberately not used: the editor is a docked, dense-widget tool
(Outliner, Details, Content Browser around a 3D viewport), which is Widgets territory.

- `./src`   — C++ (logic, behaviour, models, and any dynamic / runtime-built UI).
- `./qt`    — `.ui` files (Designer-owned layout), organised per component
- `./tests` — `editor_tests`

## What belongs in a window, and what does not

A `*Window` owns its widgets, where they are rooted, and what its signals mean. Everything else
lives beside it, and the split is by responsibility rather than by line count:

- **A job that is not the window's** gets its own directory. `src/Import/` is the worked example:
  importing a glTF is not something a file browser does, so `import_writers` (what an import writes),
  `import_pipeline` (the cook behind its loading screen) and `drop_import` (what a drop takes and
  what it runs) sit outside `Windows/` entirely, and the Content Explorer is one caller.
- **A stateful job the window drives** becomes a collaborator type it owns —
  `AssetOperations` for the Content Explorer's on-disk actions, `MaterialGraphSet` for the material
  editor's graphs-and-submeshes table. Where such a type moves the ground out from under a view, it
  says so by signal rather than reaching for the view (`AssetOperations::DirectoryDeleted`); a
  collaborator that touched a model would just be the window again under another name.
- **A rule that takes what it needs** becomes a free function in a `lower_case` file:
  `asset_rules`, `material_io`, `graph_compiler`, `material_graph`. This is also the *only* way most
  editor behaviour becomes testable — see § What is testable below.
- **Widget assembly** built in code rather than Designer goes to its own `*_ui` file
  (`material_editor_ui`), which builds and connects nothing. The `connect` calls stay in the window,
  because what a widget *does* is behaviour.

## Loading a mesh into a viewport: prepare on the worker, commit on the render thread

One `Renderer` thread draws **every** viewport in the editor, so a closure that reads a file or
cooks a mesh inside `Invoke` does not stall one panel — it stops the frame loop for all of them, and
the GUI thread is blocked waiting on it meanwhile. A playing animation stops dead.

So a load splits in two. Everything up to the upload — the `.bmesh` read, the meshlet cook, the
texture decodes, a `.bvat` bake, a posed-box measurement, the CPU picking copy — runs inside the
window's existing `background::RunWithLoadingScreen` worker, through `game::PrepareMesh` and its
tier twins (see `libs/gamelib/CLAUDE.md`). The `Invoke` closure that follows does nothing but hand
those payloads to `AssetManager::Acquire*(PreparedMesh)` and place the instances.

The prepare is a free function per panel — `editor::PrepareAnimationDraws` (`animation_draws.h`) and
`editor::PrepareMeshPreview` (`mesh_preview.h`) — for the usual reason: it is the only way any of it
is testable, since neither window can be constructed in a test.

One refusal cannot be moved: a submesh whose material does not resolve to `kPBR` is judged by the
*commit*, which needs the scene's material handles. A panel that falls back on that still reads on
the render thread, and that is the price of an asset the tier cannot draw.

## Rules

- Qt is editor only don't link to other targets
- **Generated `ui_*.h` and moc files are build artifacts** in the build tree — never
  edit or commit them; just `#include "ui_<Name>.h"`.

# editor_tests

**Catch2**, like every other suite in the repo. `Qt6::Test` is linked, but it is not the
test framework: what comes from it is `QSignalSpy` and the input simulation
(`QTest::mouseClick`), both of which are ordinary Qt classes and work fine inside a
`TEST_CASE`.

QTest was tried first and dropped. It handles exceptions badly — and this code throws
everywhere (`Project::Open`, all of `assetlib`, and cancellation *is* an exception) — its
results vanish through a Windows pipe, and a second framework meant a second CLI, which is
why `just test` cannot forward arguments to a suite. The only thing genuinely lost is
`QTRY_*`, replaced by `editor::test::WaitFor` in `tests/src/util/QtSupport.h`.

```bash
just test editor                        # the suite; about fifteen seconds
just run editor_tests -- "[assetimporter]"  # one tag
just run editor_tests -- "~[render]"    # skip the GPU cases; back to about a second
just run editor_tests -- --list-tests
```

Nearly all of that fifteen seconds is `CreateGraphics`, which every `[render]` case pays
(Catch2 re-runs a `TEST_CASE` body per `SECTION`, so a multi-section one pays it again each
time). Everything else still runs on the CPU in about a second.

## Adding a suite

One file per subject in `./tests/src`. Plain `TEST_CASE`s — no `Q_OBJECT`, no moc, and
AUTOMOC is off for this target. Tag every case (`[project]`, `[materialgraph]`, …) so it
can be run on its own. `main.cpp` exists only to stand a `QApplication` up before Catch2
runs anything.

Name a case for the behaviour it pins, not the function it calls ("The sink cannot be
deleted"), because the failure line is the bug report.

`util/QtSupport.h` carries `WaitFor` (pump the event loop until a predicate holds — needed
for anything Qt does off-thread, like `QFileSystemModel` scanning a directory) and the
`QString` printers, without which Catch2 renders a failed comparison as `{?}`.

## What is testable, and what is not

What blocks coverage is the **window**, not the device. `RenderTargetWindow`'s constructor
calls `CreateRenderTarget` with `winId()` and `headless = false`, and does not guard a null
device — so `RenderTargetWindow`, `LevelEditorWindow`, `MaterialPreviewWindow`,
`AnimationPreviewWindow` and `MainWindow` (whose constructor creates the device) are **not
covered**. Covering them needs a seam first: a `headless` flag on `RenderTargetWindowDesc`.

A fake `IGraphics` is **not** that seam. `MaterialEditorWindow`, `AssetThumbnailCache` and
`TextureNode` each degrade when their `Renderer` is null, and no shipping path produces one —
a fake would buy coverage of three branches no user reaches, and nothing else. What a failing
device does instead is leave through `main`, which reports it and exits; only the viewports
still have no way to stand without one.

What *is* testable is a rule lifted clear of the window: `CachedMaterial` and
`StampedPixmapCache` hold the ones the caches are built on. Reach for that shape before
reaching for a fake — and only where there is a rule worth pinning, not to give a passive
value somewhere else to live.

A **device alone is fine**. `editor_tests` links `bgl_d3d12_agility` (on the executable — see
`tests/CMakeLists.txt` for why an OBJECT library cannot carry it through `editor_lib`), so a
test may call `CreateGraphics` and render headlessly. `AssetThumbnailCache` is the one renderer
built that way — it owns a headless target and needs no `winId()` — and is covered end to end
in `AssetThumbnailCache_test.cpp`, which renders a real `.bmesh` and a real `.bmaterial` and
writes each to `assets/golden/thumbnail_*.got.png` to be looked at. Tag such cases `[render]`
so they can be skipped.

Everything else runs on the CPU in about a second, because the pieces that matter were
already built to work without a device: `MaterialEditorWindow` degrades to "No graphics
device", and `TextureNode` takes a null scene and a null preview cache on purpose. The
tests lean on exactly that.

A `MaterialEditorWindow` **without a device has no submesh graphs at all** — they are built
from the preview's geometry, and there is no preview. So its per-submesh behaviour cannot
be driven through the window. Where such a rule is worth pinning, lift it into a free function
that takes what it needs (`editor::IsSameMaterialFile` in `material_io.h`, `OutputCentre` in
`material_graph.h`) and test that. Both of those paid for themselves the day they were written,
each catching a bug in the code they were extracted from.

`editor::PrepareMeshPreview` and `editor::PrepareAnimationDraws` are the same shape applied to the
two viewport loads, and they pin something a window test could not reach anyway: that the work
happens *before* the render thread is asked for anything. `tests/src/util/MeshFixture.h` builds the
`.bmesh` those need — with real meshlet streams in it, because a cook reads them.

Two things a test cannot drive, and why:

- **Modal dialogs** (`QFileDialog`, `QMessageBox`, `QInputDialog`, `QMenu::exec`) are
  called directly on the concrete Qt types, with no injection seam. Triggering one from
  a test hangs it. This is what keeps `editor::import::ImportMesh`, `AssetOperations`'
  Delete/Rename/Bake, `MainWindow::NewProject`/`OpenProject`/`CleanUnusedTextures`, and
  `MaterialEditorWindow`'s save/open uncovered. Hoisting a rule out into a free function that
  takes what it needs is what unlocks it, and the import is the worked example:
  `editor::import::WriteMaterials`, `WriteRig` and `RollBack` are each driven directly by a
  test, so what an import *writes* and what a failed one *deletes* are pinned even though the
  import itself is not.
- **A `Drop` event** cannot be synthesized: Qt only delivers one to a widget that is
  mid-drag, and that state belongs to the platform's drag session. `DragEnter` *can* be
  posted, so drop *routing* is covered that way and the drop *rules* are driven straight
  through the handler.

`background::RunWithLoadingScreen` is testable despite its nested event loop and modal
screen: arm `editor::test::OnLoadingScreen` (`tests/src/util/Modal.h`) **before** the
call, and it drives the screen from inside the loop. Two rules there:

- A worker that waits must have a **deadline**. The screen refuses to close while the
  worker runs, so a worker that waits forever hangs the suite rather than failing one test.
- A worker must never pump the event loop (`WaitFor`) — it is not on the UI thread. Block
  on an atomic instead.
