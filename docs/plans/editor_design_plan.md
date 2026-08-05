# The editor's design — findings and plan

A survey of every file under `apps/editor/src` (59 files, 9012 lines; 78 with the suite) against
`gamelib`'s `AssetManager`, the layering rules in the root `CLAUDE.md`, and the testability limits
`apps/editor/CLAUDE.md` already documents.

Nothing here is a rendering bug. The findings are all about where behaviour lives: the editor has a
UI layer and a `bgl` layer and nothing in between, so every rule about assets is written inside a
`QWidget`, next to the dialog that asks for it.

## 1. What the survey found

### Behaviour lives in widgets, which is why so little of it is tested

`ContentExplorerWindow.cpp` is 1026 lines: file browsing, glTF import, HDR import, rollback,
reference-checked deletion, and material baking. `MaterialEditorWindow.cpp` is 1035: the open-graph
set, the submesh↔graph mapping, compilation to a `bgl` material, `.bmesh` rewriting, and the file
dialogs. `MainWindow`'s constructor parses the config file, creates the device, and owns the project
lifecycle in one 90-line block.

`apps/editor/CLAUDE.md` already itemises what that costs: `ImportMesh`, `RollBack` ("the code most
worth testing"), `BakeMaterial`, `DeleteAsset` and every `MainWindow` project action are uncovered,
because each sits behind a modal dialog called directly on a concrete Qt type. The document's own
prescription — "hoisting those rules into a GUI-free header would unlock them" — is right, and
`ContentExplorerWindow::WriteImportedMaterials` is what following it one function at a time looks
like: a public `static` taking five parameters so a test can reach past the widget.

### The editor rebuilds the seam it is supposed to reach for

`game::AssetManager` owns `AcquireMesh`, `CreateInstance`/`DestroyInstance`, `CreateSphere`,
`AcquireEnvironment`/`ReleaseEnvironment` and `SetInstanceSubmeshMaterial`. Two of the editor's three
renderers use none of it:

| | placement | materials | environment |
|---|---|---|---|
| `MaterialPreviewWindow` | hand-rolled (`:276-322`) | neutral default for every slot | `editor::ApplyEnvironment` |
| `AssetThumbnailCache` | hand-rolled (`:529-563`) | `AssetManager::AcquireMaterial` | `editor::ApplyEnvironment` |
| the level editor | — | — | — |

The two placement loops are the same forty lines twice: the same node→geom walk, the same
`geomForMesh` map, the same AABB accumulation, the same `submeshCount` iteration. The environment
column is not the same finding — `ApplyEnvironment` and `AcquireEnvironment` differ structurally, not
in error policy, and § 2 records why it stays.

Root `CLAUDE.md` states the rule at stake — *`AssetManager` holds the **only** implementation of the
baked-vs-loose branch … a material must render the same however it was loaded.* The duplicated
**walk** breaks it. The differing **material choice** does not, and must not be unified: the preview
binds the neutral default deliberately, because it exists to author one material and the graph rebinds
it per submesh through an instance override. The thumbnail showing the asset as it will actually
render, and the preview showing it as it is being authored, is correct. What is wrong is that the two
reach that point through two copies of the same code, either of which can drift alone.

### Nothing in the editor is a document

- `m_Ui.actionSave` is enabled and disabled (`MainWindow.cpp:459`, `:484`) and **never connected**. It
  has no shortcut. It is a menu item that does nothing.
- `menuEdit` is empty in `MainWindow.ui`, and is enabled and disabled alongside it.
- QtNodes gives every `DataFlowGraphicsScene` a `QUndoStack` (`BasicGraphicsScene::undoStack()`).
  Nothing reaches it. There is no undo anywhere in the editor.
- Nothing records whether a graph has unsaved edits. `SelectSubmesh` swaps the board, `Reset()`
  discards every graph when the project changes, and `closeEvent` accepts unconditionally. All three
  drop authored work with no prompt.

The absence shows up in the plumbing too: a graph is an `int` index into `m_MaterialGraphs`, a second
`int` index through `m_GraphForSubmesh`, and `-1` for both kinds of "none", threaded through some
twenty methods with a `static_cast<size_t>` at each use.

### The deletion guard knows about exactly one window

`ContentExplorerWindow` takes an `AssetsHeldOpenFn`, wired at `MainWindow.cpp:145` to
`m_MaterialEditor->OpenMaterialPaths()`. The header argues correctly that it must not be defaultable,
because a guard that can be left unwired fails open silently. The shape is the problem rather than the
default: the guard is a callback naming one window, so the next window that opens an asset — a second
material tab, a level editor that loads one — reintroduces exactly the failure the parameter was
written to prevent, and nothing fails to compile.

### The threading contract is comments rather than structure

`Renderer` is a good seam. What is around it is not:

- `Invoke` is a blocking round-trip onto a thread whose loop is a vsync-locked `Present`, and it is
  called from GUI hot paths: `TextureNode::SetTexturePath`, `MaterialEditorWindow::CompileGraph` on
  every alpha-mode or occlude change, `RenderTargetWindow::SyncSize`, and twice per drain turn in
  `AssetThumbnailCache::RenderNextQueued` — on a zero-interval timer.
- GUI-owned state is written on the render thread. `MaterialPreviewWindow::LoadMesh` fills
  `m_SubmeshNames`, `m_SubmeshMaterialPaths` and `m_SubmeshRefs` *inside* its `Invoke` closure
  (`:288-301`); `SetSubmeshMaterial` reads `m_Geoms`/`m_Instances` from a `Post`ed one. It is correct
  today only because every GUI-side mutation happens to sit behind an `Invoke` that drains the queue
  first — an invariant no signature carries.
- `FrameStatsUpdated` is emitted from the render thread and is safe only because
  `MainWindow::SetUpFrameStats` remembers `Qt::QueuedConnection`.
- Teardown order is hand-maintained: `MainWindow::ReleaseRenderResources`, the member-declaration
  comment in `MainWindow.h`, and `closeEvent`. One more widget that touches the renderer breaks it
  silently. E5 named the sequence rather than leaving it in `~MainWindow` alone, because a
  constructor that fails part-way needs the same order and does not get a destructor.

### The two caches are one cache, and it renders every tile twice

`TexturePreviewCache` and `AssetThumbnailCache` each carry an identical
`QCache<QString, {QPixmap, qint64}>`, an identical `FileStamp`, an identical stamp-comparing `Lookup`,
an identical `Request` (in-flight set, stale eviction), the same `costKb` arithmetic, the same
`c_BudgetKb = 64 * 1024`, a `QThreadPool` at two threads, and a ready signal. The thumbnail header
says so outright: *"the twin of TexturePreviewCache, and deliberately the same shape"*.

The duplication hides a bug in one of them. `Request` inserts into `m_InFlight`;
`RenderNextQueued` removes it at dequeue (`AssetThumbnailCache.cpp:453`), but the cache entry is not
inserted until the capture resolves on a **later** drain turn. In between, any repaint reaches
`AssetFileModel::data()` → `Lookup` misses → the path is neither in flight nor cached → a second
`LoadTask` (a full `.bmesh` read plus a full Basis transcode) and a second GPU render of the same
asset. Every visible tile pays it at least once.

### Startup cannot fail, so the graceful path is unreachable

`MainWindow`'s constructor reads 30 lines of `settings["x"]["y"].GetOrDefault(...)` inline
(`:42-118`). Defaults are split between the `bgl` structs and literals here; an unknown or mistyped
key is silently the default; there is no type to enumerate or test. The same file mixes machine
settings (debug layer, GPU validation, descriptor budgets) with workspace state (`startupProject`)
with per-feature asset paths (`thumbnails.environmentMap`, `materialEditor.dataRoot`).

`main()` has no `try`/`catch` around `MainWindow window;`, and `Renderer`'s constructor throws
whenever `CreateGraphics` or `CreateScene` fails — so a bad driver, a missing Agility SDK or a
too-large budget leaves through `std::terminate` and a crash log rather than a message.

The consequence is worth stating plainly: `MaterialEditorWindow`, `AssetThumbnailCache` and
`TextureNode` each degrade carefully when `renderer == nullptr`, and **no shipping path can produce
that**, because `MainWindow` either gets a `Renderer` or dies. Those branches exist only for the
tests that `apps/editor/CLAUDE.md` describes leaning on them.

### Smaller things, each cheap

- `MaterialEditorWindow::RefreshActions` (`:528`) does `assetlib::loadMaterial` plus
  `assetlib::bakeIsStale` — which walks the material's routes against the disk — on every submesh
  selection change, every output-type change, every `SetDataRoot`, every save and every bake.
  `SetPreviewGeometry` additionally `exists()`-checks per submesh.
- `LogToFile` (`main.cpp:15`) opens, writes and closes `editor.log` per message with no mutex. Qt
  requires an installed handler to be thread-safe, and `qWarning`/`qCritical` are called from the
  render thread (`Renderer::Frame`, `RenderTargetWindow::ReportFrameTiming`). `ReportFrameTiming`
  logs *per missed vblank*, so a hitch makes the render thread open a file, which lengthens the hitch.
- `AssetImportWindow` (`.h`, `.cpp`, `.ui`) is referenced by nothing.
- `TryLoadTexture` is defined in the anonymous namespaces of `AssetThumbnailCache.cpp:52` and
  `MaterialPreviewWindow.cpp:32` — the same function twice, differing only in its warning tag — and
  called from neither. Both were superseded by `editor::ApplyEnvironment`.
- `bmesh::NameFromPool` duplicates `assetlib::nameFromPool` (`bmesh_io.h:97`). The explorer uses the
  assetlib one, the preview the editor's copy.
- `MainWindow.h:84` calls `m_Assets` "the editor's one asset manager, over the Level Editor's view".
  `LevelEditorWindow` is an empty subclass of `RenderTargetWindow` that uses no assets; only the
  thumbnail cache touches `m_Assets`.

## 2. Design

### An asset-operation layer, under the widgets

A new `apps/editor/src/Assets/`, GUI-free apart from `background::Progress`, holding what the
explorer and the main window do to a *project* rather than to a widget: import a mesh, import an
environment, bake a material, delete an asset, prune unused textures. Each takes a request struct and
returns a result struct; each is a free function, because none of them own state.

The widget keeps the parts only a widget can do — reading a dialog, showing a `QMessageBox`, deciding
whether a multi-file drop carries on — and calls one function per operation.

The logic is not rewritten. `ImportMesh`'s conflict check, its rollback, and its "write every material
before pointing a submesh at one" ordering move across as they are; what changes is that a test can
call them.

*Rejected: a `QObject` service with result signals.* It would give stateless operations an identity
and a lifetime, and a test would need an event loop to observe an outcome that is already a return
value. *Rejected: more `static` members on the widget, as `WriteImportedMaterials` is.* That works
once. The fifth one is a widget header with five unrelated public statics, still owning the state they
need, and the class stays 1000 lines.

### `MaterialDocument`, and a registry of what is open

One type replacing the `MaterialGraph` struct and the two `int` maps around it: the material path, the
model, the scene, the preview handle, the submeshes it drives, its `QUndoStack` (taken from the
QtNodes scene the editor already builds), and a dirty flag set from `MaterialOutputNode::Changed` and
cleared on save. Documents are held by `std::unique_ptr` and referred to by pointer, so the `-1`
sentinels go.

Alongside it, an `OpenDocuments` registry: a window registers an absolute path when it opens one and
unregisters when it closes it; `ContentExplorerWindow` queries the registry instead of holding a
callback into `MaterialEditorWindow`.

`Save`, `Undo` and `Redo` become actions on the focused document, which is what finally connects
`actionSave` and fills `menuEdit`. The three discard paths — submesh switch, `Reset`, `closeEvent` —
ask before dropping a dirty document.

*Rejected: a dirty flag beside the existing parallel vectors.* The flag is the cheap half; the bug
class is the index plumbing, where `m_MaterialGraphs` and `m_GraphForSubmesh` index into each other
and `-1` means two different things. *Rejected: deriving dirtiness by comparing `model.save()` against
the file.* It is exact, and it costs a full graph serialize per check on a path that already runs per
keystroke. *Rejected: each window keeping its own open-set and the explorer subscribing to a signal
per window.* A subscription is order-dependent at construction and fails open silently when someone
forgets one — the failure the current callback already has. A registry a window must call to open a
document at all cannot be skipped quietly.

### One placement path, in `gamelib`

A free function in `gamelib` — `game::PlaceMesh(scene, view, mesh, materials, fallback)` — walking an
**already-decoded** `assetlib::BMesh` and returning what both callers build by hand today: the geoms,
the instances, the submesh table, and the bounds. The caller owns what comes back and releases it, as
both do now.

The preview keeps passing its neutral default and the thumbnail keeps passing the resolved materials.
That difference is the point of each and is preserved; what converges is the walk.

`fallback` is the fifth parameter because the two loops handle a missing material differently, and one
of them has to change. The thumbnail overrides per instance whenever `submesh.material` is out of
range (`AssetThumbnailCache.cpp:550-551`); the preview has no such branch and instead pads its vector
to `std::max<size_t>(1, mesh.materials.size())` (`MaterialPreviewWindow.cpp:264-266`), which covers a
mesh naming no materials at all but not a submesh naming an index past the end. `PlaceMesh` takes the
override branch as the rule and the padding goes: both callers pass their neutral default as
`fallback`, and a submesh whose index is out of range gets it. **This changes the preview's
behaviour** in the one case the padding never covered, from whatever `AddStaticMesh` does with an
out-of-range index to a defined default — which is why E9's gate places a mesh with a deliberately
bad material index.

*Rejected: routing both callers through `AssetManager::AcquireMesh`.* It is the obvious move and it
does not work. `AcquireMesh` keys geometry by `"{path}#{meshIndex}"` (`AssetManager.cpp:251`) and a
geom's submesh materials come from the file and are shared by every instance of it
(`AssetManager.h:218-219`) — so the same `.bmesh` placed by the preview and by the thumbnail through
one path-keyed geom hands one caller the other's materials, destroying the very difference this
design preserves. `AcquireMesh` also reads the file itself, on the calling thread, while both callers
already hold a decoded `BMesh` — the thumbnail cache decodes it on a worker on purpose
(`AssetThumbnailCache.cpp:105`). Hence a free function over a decoded mesh, outside the path cache,
rather than a method on the manager.

*Rejected: a shared `editor::PlaceMesh` helper.* Two copies become one, in the wrong layer — the root
`CLAUDE.md` puts "load this asset into a scene" in `gamelib`, and a level editor would have to reach
into `apps/editor` for it. *Rejected: making the preview resolve the mesh's real materials so the two
agree outright.* It would fight the instance-override model the material editor is built on, and
rewrite what the artist is authoring against.

*Not collapsed: `editor::ApplyEnvironment` onto `AssetManager::AcquireEnvironment`.* An earlier draft
of this plan called the difference error policy. It is four differences, and they are structural:
`ApplyEnvironment` **binds** to a view (`environment.cpp:38,49,64`) where `AcquireEnvironment`
deliberately does not (`AssetManager.h:120-123`); it takes an absolute `.benv` plus an explicit data
root where the manager resolves against its own; `ReplaceEnvironment` frees through raw
`DeleteTextureAsset` (`environment.cpp:86`) where the manager's handles are refcounted; and decisively,
`MaterialPreviewWindow` calls it from its **constructor** (`:118`), while `m_Assets` does not exist
until `MainWindow::SetActiveProject` (`:375`) and is replaced on every project change. The preview has
no `AssetManager` to reach at the moment it needs an environment. `ApplyEnvironment` stays.

### `StampedPixmapCache`

The `QCache`, the mtime stamp rules, the in-flight set, the cost arithmetic and the ready signal,
once. `TexturePreviewCache` and `AssetThumbnailCache` supply only "produce a pixmap for this path" —
synchronously off a pool for one, split-phase through the GPU for the other. The in-flight entry lives
until the cache entry lands or the work is abandoned, which is the duplicate-render fix, in one place.

*Rejected: a template over the payload type.* Both payloads are `QPixmap`; it would be one
instantiation and more syntax. *Rejected: one cache class with a kind enum.* The two production paths
share nothing, so the merged class is two disjoint halves behind a branch.

### A startup that fails with a message

`main()` catches. A graphics failure, or a `config.json` that will not parse, becomes a message box
and a non-zero exit instead of `std::terminate` and a crash log.

**No `EditorConfig`.** An earlier draft of this plan called for one: a struct holding every key, with a
`Parse(const core::Settings&)` that validated them. It was written, reviewed on #279, and rejected --
414 lines of type and test to replace thirty lines of `settings["x"]["y"]`, wrapping a class whose
whole job is already to be read that way. The argument that it caught a lost default did not survive
inspection either: the default was only at risk *because* the parse was being moved.

`core::Settings` is the config abstraction. `MainWindow` keeps reading it directly, with its defaults
as the `GetOrDefault` fallbacks beside each key.

**It does not become a degraded editor**, and an earlier draft of this plan said it did. That draft
claimed the null-`Renderer` editor "already exists": it does not.
`RenderTargetWindow`'s constructor dereferences `m_Desc.renderer` unguarded
(`RenderTargetWindow.cpp:46`) though its destructor guards (`:68`); `MainWindow` builds a
`LevelEditorWindow` unconditionally (`:91`) and calls `m_Renderer->Invoke` in both `closeEvent`
(`:188`) and its destructor (`:203`). What degrades is `MaterialEditorWindow` (`:202`, `:237`),
`AssetThumbnailCache` and `TextureNode` — never a viewport, which is exactly what
`apps/editor/CLAUDE.md` means by "`RenderTargetWindow` … does not guard a null device".

So those three degraded branches stay test-only, and this plan stops claiming otherwise. Giving the
viewports a no-device path is a change to `RenderTargetWindow`'s contract — a placeholder where a
swapchain goes — not to config parsing, and it belongs with the threading work § 5 defers, which is
the other thing that rewrites how a viewport holds its state.

*Rejected: validating inside `core::Settings`.* It is a generic reader used elsewhere; the editor's
schema is not its business. *Not attempted: warning on unrecognised keys.* `core::Settings` exposes
`operator[]`, the conversions, `GetOrDefault` and `IsNull` and no way to enumerate what a file
contains (`Settings.h:20-60`; the `nlohmann::json` is private). Detecting a stale key needs an
enumeration accessor there — which would be legitimate, since "what does this file contain" is a
generic reader's business, unlike the editor's schema — but it is a `core` change in service of a
warning, and out of scope for this step rather than ruled out for good.
*Rejected: a fake `IGraphics` so the degraded paths get a test* — `apps/editor/CLAUDE.md` floats it.
It buys coverage of three branches and nothing else, and now that E5 no longer routes a real failure
through them, those branches are on no shipping path at all: an interface whose only caller is a test,
testing code no user reaches.

## 3. What changes, and what it can break

| subsystem | change | what can break |
|---|---|---|
| `src/Assets/` (new) | the five project operations move out of the widgets | import/bake/delete regress silently — none of them is covered today, which is why the tests land with the move |
| `ContentExplorerWindow` | loses ~500 lines; keeps drops, menus, dialogs | drop routing and the multi-file cancel rule, both currently pinned by tests |
| `MaterialEditorWindow` | `MaterialGraph` + two `int` maps → `MaterialDocument` | the shared-graph rule (submeshes naming one material share one document and one preview handle) — reachable only with a device, so it needs a `static` extracted for it |
| `MainWindow` | `actionSave`/`menuEdit` gain owners | startup ordering, and the hand-ordered destructor, which is untested in any form |
| `Thumbnails/` | both caches rebase on `StampedPixmapCache` | thumbnail staleness on re-bake, covered; the drain/capture split, covered by the goldens |
| `gamelib` | gains `PlaceMesh` over a decoded `BMesh`, and `bmesh::WorldTransform`/`GrowBounds` move into it from the editor | nothing existing — `PlaceMesh` is an addition and `AssetManager`'s path-keyed cache is deliberately untouched; the two moved helpers have no other caller |
| `apps/editor/src/Mesh/` | emptied and deleted — E1 takes `NameFromPool`, E9 the other two | nothing; `BMeshUtil.h` holds exactly three functions and no fourth thing lives there |
| `main.cpp` | mutex on the log handler; `try`/`catch` around the window | nothing reads `editor.log` programmatically; the risk is losing messages, not correctness |

Two files carry most of the risk and neither has a test that would catch a mistake:
`MainWindow`'s destructor ordering, and the preview's shared-graph rule.

### The docs each step falsifies

`CLAUDE.md` requires a doc updated in the commit that invalidates it, so each of these belongs to a
step rather than to a sweep at the end:

| doc | falsified by | what changes |
|---|---|---|
| `apps/editor/scripts/add_qt_class.py` | E1 | its usage example and `--help` name `Windows/AssetImport/AssetImportWindow`, the class being deleted |
| `apps/editor/CLAUDE.md`, "What is testable" | E5, E7 | the modal-dialog list naming `ImportMesh`, `BakeMaterial`, `NewProject`, `OpenProject`, `CleanUnusedTextures`; and "a fake `IGraphics`" floated as the seam |
| `libs/gamelib/CLAUDE.md`, "identity is the path" | E9 | a placement that deliberately sits outside the path-keyed cache |

## 4. Staging

Each step builds and passes on its own.

**E1–E5 land on `master`, one PR each.** They are independent of each other and of everything below,
and each leaves the tree in a complete state — two of them are live bugs, and a feature branch would
hold them back behind work they do not depend on.

**E6–E9 land on `feat/editor-design`.** These interlock: E6's documents are what E7's operations ask
about and what E8's registry registers, so `master` would be half-migrated between them. The branch is
cut from a `master` that already carries this plan and E1–E5, so there is nothing to rebase.

### On master

* **E1 — the sweep.** Delete `AssetImportWindow`, its `.ui` and the two directories that empties
  (`src/Windows/AssetImport/`, `qt/Windows/AssetImport/`); both dead `TryLoadTexture` definitions; and
  `bmesh::NameFromPool` in favour of `assetlib::nameFromPool`. Repoint `add_qt_class.py`'s example at
  a class that still exists. Fix the `m_Assets` comment in `MainWindow.h`.
  *Gate:* `just build` and `just test editor` — a dead-code deletion's gate is that nothing referenced
  it, which the linker settles; plus `add_qt_class.py --help` naming something real.
* **E2 — the log handler.** A mutex and a held-open `QFile`; the per-missed-vblank `qWarning` becomes
  a counter, which `FrameStatsUpdated` already aggregates and the status bar already shows.
  *Gate:* a test that drives the handler from two threads and asserts no interleaved line; and a
  viewport stall no longer writing a line per frame.
* **E3 — `StampedPixmapCache`.** Lift the shared half out of the two caches; keep the in-flight entry
  until the cache entry is inserted or the render is abandoned.
  *Gate:* a case in `AssetThumbnailCache_test` pinning that one `Request` yields one decode and one
  render across repeated `Lookup` misses — the duplicate-render bug, written as a failing test first.
  The matching case in `TexturePreviewCache_test` passes from the start, because `Deliver` clears the
  in-flight entry and inserts the cache entry in one call (`TexturePreviewCache.cpp:138`, `:151`);
  it pins behaviour the shared base must not lose.
* **E4 — `RefreshActions` stops reading the disk.** Cache the loaded `BMaterial` and its mtime on the
  editor; refresh on save and on `MaterialBaked`, not on selection.
  *Gate:* a case pinning one `loadMaterial` per save rather than one per submesh change.
* **E5 — a catching `main`.** A startup failure that leaves through a message box rather than
  `std::terminate`. It also updates `apps/editor/CLAUDE.md`, whose "fake `IGraphics`" suggestion this
  step declines.
  *Gate:* two launches -- a deliberately broken `config.json`, and a failure injected late in the
  constructor -- each confirming a logged critical, a message, and no crash log where master
  terminates. The second is the one that matters: catching makes the unwind of a half-built
  `MainWindow` reachable for the first time, and Qt deletes the viewports as children *after* the
  members, so `~RenderTargetWindow` would otherwise run with `m_Renderer` already gone.

### On feat/editor-design

* **E6 — `MaterialDocument`.** The type, the dirty flag, the undo stack surfaced into `menuEdit`,
  `actionSave` connected, and prompts on the three discard paths. The `int`/`-1` plumbing goes with it.
  *Gate:* cases for dirty-on-edit, clean-on-save, and that switching submesh with a dirty document
  asks; plus the shared-graph rule extracted as a `static` and pinned, since the window cannot reach
  it without a device.
* **E7 — the asset-operation layer.** One operation per PR: prune, bake, delete, import environment,
  import mesh — smallest first, so the modal seam is settled before `ImportMesh` and `RollBack` move.
  The last of them updates `apps/editor/CLAUDE.md`, whose modal-dialog list names the operations this
  step makes reachable.
  *Gate:* per operation, the test the move unlocks. `RollBack` lands with a test that a cancelled
  import leaves nothing behind, which is the whole reason it exists.
* **E8 — `OpenDocuments`.** The registry, `MaterialEditorWindow` registering into it, and
  `ContentExplorerWindow`'s `AssetsHeldOpenFn` replaced by a query.
  *Gate:* `ContentExplorerWindow_test` drives the guard through the registry rather than a
  hand-supplied lambda, including the case the callback shape cannot express — a second window
  holding the asset.
* **E9 — one placement path.** `game::PlaceMesh` added to `gamelib`, over a decoded `BMesh` and
  outside the path-keyed geom cache; the two hand-rolled loops deleted. `bmesh::WorldTransform` and
  `GrowBounds` move into `gamelib` with it — both loops call them and `PlaceMesh` needs them there —
  which, with E1 having taken `NameFromPool`, empties `BMeshUtil.h`/`.cpp` and `src/Mesh/`; delete
  them. `ApplyEnvironment` is left alone — see § 2. Updates `libs/gamelib/CLAUDE.md`, since the new
  call sits outside the identity rule that page states.
  *Gate:* two `gamelib` tests — that the same `BMesh` placed twice with different material vectors
  keeps them apart (the property the rejected `AcquireMesh` route would have broken), and that a
  submesh naming an out-of-range material index gets `fallback`; then
  `just run editor_tests -- "[render]"` with the thumbnail goldens looked at, `just test` for the
  other `gamelib` consumers, and the preview compared against the tile for the same mesh.

E6 precedes E7 and E8. E9 depends on none of them and can be taken at any point on the branch.

## 5. What this does not do

- **No level editing.** `LevelEditorWindow` stays an empty `RenderTargetWindow` subclass, and the
  `Levels` category a project is scaffolded with (`Project::c_LevelsDirectoryName`) stays a directory
  nothing writes to. E8's registry and E7's layer are what a level editor would build on, which is why
  they come first, but nothing here loads or saves a level.
- **No undo for asset operations.** E6's undo stack covers graph edits only. Deleting an asset and
  baking a material stay irreversible, and keep saying so in their confirmations.
- **No priority or cancellation for thumbnail requests.** `AssetFileModel::data()` still queues work
  for every tile it paints, and a tile that scrolls away is still rendered. E3 makes that one place to
  fix rather than two; fixing it needs a viewport-aware request policy, which is its own change.
- **Nothing about the threading contract's structure, and no viewport that survives a missing
  device.** The findings in § 1 stand: `Invoke` stays blocking on GUI hot paths, GUI state stays
  written inside render closures, teardown order stays hand-maintained, and `RenderTargetWindow` still
  dereferences its `Renderer` unguarded in its constructor. These belong together — each rewrites how
  a viewport holds its state — none is a prerequisite for the steps above, and they want a plan of
  their own, taken once E6 has settled what owns what.

## 6. Risk

The two that can break a working editor are E6 and E9.

**E6** reaches across `MaterialEditorWindow` — `int graphIndex` is a parameter in five signatures
(`MaterialEditorWindow.h:110`, `:118`, `:146`, `:149`, `:152`) and some twenty methods touch the index
plumbing — and the shared-graph rule is easy to lose in the translation.
`MaterialEditorWindow_test` covers the device-free half; the half needing a preview is not covered and
cannot be, so that rule wants a `static` extracted for it in the same spirit as `IsAlreadyDefault` and
`OutputCentre`, both of which caught a bug the day they were written.

**E9** changes what the GPU is handed in both the preview and the thumbnails, and the thumbnail
goldens are `.got.png` files to be looked at rather than compared. A regression there is silent until
someone looks. Its `gamelib` half is an addition, so nothing existing moves under the examples — the
risk is concentrated in the two call sites it replaces. Take it alone, and look at the goldens.

E7 is large but low-risk: each operation moves whole, and the compiler finds every caller. Its real
cost is the modal seam — the operations are testable only once the dialogs they call are behind
something a test can answer, which is why E6 introduces that seam for one prompt before E7 needs it
for five.
