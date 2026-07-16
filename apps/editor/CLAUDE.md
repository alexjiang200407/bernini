# editor

editor is the Bernini game editor: a desktop application for authoring scenes and
managing resources. It is also the offline asset-cook host — artists export glTF, the
editor imports it (via assetlib) and converts it into the game-ready format.

- CMake targets: `editor_lib` (everything), `editor` (just `main.cpp`), `editor_tests`.
  Built **automatically only when Qt6 is found** — the root `CMakeLists.txt` probes
  `find_package(Qt6 ...)`; there is no manual `BUILD_EDITOR` flag.
- Windows-only for now.
- CMake: `./CMakeLists.txt`
- Links `gamelib` as well as `bgl` and `assetlib`. `gamelib` is the seam that owns "load this
  asset into a scene", and its `AssetManager` holds the **only** implementation of the
  baked-vs-loose branch that turns an `assetlib::BMaterial` into a `bgl::MaterialHandle`.
  Reach for it rather than rebuilding that branch — a material must render the same however
  it was loaded.

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
just run editor_tests -- "[project]"    # one tag
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
device — so `RenderTargetWindow`, `LevelEditorWindow`, `MaterialPreviewWindow` and
`MainWindow` (whose constructor creates the device) are **not covered**. Covering them needs
a seam first: a `headless` flag on `RenderTargetWindowDesc`, or a fake `IGraphics`.

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
be driven through the window. Where such a rule is worth pinning, lift it out as a `static`
that takes what it needs (`IsAlreadyDefault`, `OutputCentre`) and test that. Both of those
paid for themselves the day they were written, each catching a bug in the code they were
extracted from.

Two things a test cannot drive, and why:

- **Modal dialogs** (`QFileDialog`, `QMessageBox`, `QInputDialog`, `QMenu::exec`) are
  called directly on the concrete Qt types, with no injection seam. Triggering one from
  a test hangs it. This is what keeps `ContentExplorerWindow::ImportMesh` and `BakeMaterial`,
  `MainWindow::NewProject`/`OpenProject`/`CleanUnusedTextures`, and
  `MaterialEditorWindow`'s save/open uncovered — including `RollBack`, which is the
  code that deletes files when an import fails and is therefore the code most worth
  testing. Hoisting those rules into a GUI-free header would unlock them.
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
