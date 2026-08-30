# derived-authored-editor-opt — implementation plan

The Content Explorer shows only what was authored, and a viewport takes the source rather than the
container an import made of it.

## Context

`Authored/` and `Derived/` is a sound storage rule. `originOf`/`requireOrigin` enforce it
(`libs/assetlib/include/assetlib/project_layout.h:88-97`), `AssetStore::Save` throws on a container
written into the wrong half, `pack` excludes by it, and a project's `.gitignore` is one line because
of it.

The editor exposes that storage rule as a **navigation** rule. `AssetFileModel` is a plain
`QFileSystemModel` (`apps/editor/src/Windows/ContentExplorer/AssetFileModel.cpp:11`), one row per
file on disk, so one model is scattered across two trees: `Authored/Meshes/Coyote.glb` beside
`Authored/Meshes/Coyote.bimport`, and `Derived/Meshes/AnimalFriends/Coyote/Coyote.bmesh` beside a
`.bskel` and a `.banim` in two further directories. To light a mesh in a preview you drag a
`.bmesh` out of one half and a `.benv` out of the other, and you have to know which half holds the
file each panel wants.

No comparable engine does this. Unity's Project window shows `Assets/` and never `Library/`; the row
is `Coyote.fbx` and its settings live in a hidden `.meta`. Godot's FileSystem dock shows `res://` and
never `.godot/imported/`; the row is `coyote.gltf` and its settings live in a hidden `.import`.
Unreal is the exception that proves the rule — what its Content Browser shows *is* the imported
`.uasset`, a committed file, while its true cache (the DDC) is hash-keyed, never shown and never
renamed. All three agree that exactly one file per asset is the one a person names, and that the
regenerable cache is never it.

Bernini is currently a hybrid: `Derived/Meshes/*.bmesh` is Unreal-shaped (a real, human-named,
path-referenced file) but Unity/Godot-shaped in lifecycle (gitignored, put back by
`AssetStore::Reimport`). This feature resolves it onto the Unity/Godot side.

## Decisions

- **ADR-1 — The Content Explorer shows only the authored half.** `Derived/` is never browsable.
  *Rejected: leaving `Derived/SourceTextures/` visible so the material graph's texture drop keeps
  working, because half a rule leaves in the tree the very folder that caused the confusion.*

- **ADR-2 — The `.glb` is the row that stands for an imported model; the `.bimport` is hidden.**
  Unity's `.fbx`/`.meta` and Godot's `.gltf`/`.import` exactly: the source is the row, the settings
  are a sidecar. *Rejected: showing the `.bimport` instead, because it reads as machinery rather
  than as the model; rejected showing both, because a duplicate row per model is the noise this
  feature exists to remove.*

- **ADR-3 — The explorer's browse root and the project's data root become two different things.**
  The views root at `Data/Authored`; `AssetAt` and `AssetOperations` keep resolving against `Data`,
  so no reference, mount key or stored path changes. Rooting is what hides `Derived/` — not a
  row-hiding rule — because a root the views cannot navigate above is an invariant, where a hidden
  row is a sweep that has to be reapplied on every insertion. *Rejected: keeping the root at `Data/`
  and hiding the `Derived/` row, because it leaves one redundant `Authored/` level to click through
  and makes the rule a property of `HideBuildProductRows` rather than of where the views are
  pointed.*

- **ADR-4 — A viewport takes the authored source and resolves what it needs.** A dropped `.glb`
  becomes `.bimport` via `importDocumentKeyFor`, and the `.bmesh` is read out of that document's
  `outputs`. Never ambiguous: an import writes **at most one of each** — `ImportOutputs` holds a
  single `mesh`, a single `skeleton` "written only when the source carries a skin", and a single
  `animations`, "one file holding every clip"
  (`apps/editor/src/Windows/AssetImporter/AssetImporterDialog.h:24-45`). So the mesh is always
  present and the rig may not be; `outputs` is the seam that says which exist, and the resolver
  reads it rather than assuming a fixed set. The existing `.bmesh` suffix match stays, so a path
  from a saved document still drops.

- **ADR-5 — A `.glb` dropped on the material graph canvas offers that source's extracted textures.**
  They are reached through the document's `textureDir`, not its `outputs` — `outputs` names only the
  three containers. `MaterialGraphView` keeps accepting a bare `.ktx2` for a saved graph's sake.
  *Rejected: Unity-style child rows under the source, because `AssetFileModel` would stop being a
  plain `QFileSystemModel` and its own header records why it is one — "the views index straight into
  this model in a dozen places, and a proxy would put a mapToSource in front of every one of them
  for nothing" (`AssetFileModel.h:21-22`). Rejected a file picker on `TextureNode`, which trades a
  direct gesture for a dialog.*

- **ADR-6 — Derived files are never renamed or deleted by a person, only carried.**
  `AssetOperations` refuses any target whose `originOf` says `kDerived`, so the rule is an invariant
  a test can reach rather than a side effect of which rows a view happens to hide. *Rejected:
  relying on the rows being unreachable, because the rule would then live only in a view.*
  `assetlib`'s own ability to rename and delete derived files is **not** removed: `texture_refresh`
  follows a moved texture through `RenameAsset` (`libs/assetlib/src/texture_refresh.cpp:123`) and
  `assetlib_cli` exposes a rename command (`libs/assetlib/cli/main.cpp:1105`).

- **ADR-7 — Renaming a `.glb` moves the group.** The `.bimport` and every entry in `outputs` move
  with it, and every stored reference to any of them is rewritten. This completes a rule `planRename`
  already states in the opposite direction: a `.bimport` "sits beside the source it describes and
  cannot be renamed alone" (`libs/assetlib/src/asset_rename.cpp:184-190`).

- **ADR-8 — Deleting a `.glb` does *not* sweep the group; deletion stays out of this feature.**
  It was drafted as the symmetric half of ADR-7 on the reasoning that a row which renames but never
  deletes is incoherent. It was dropped for three reasons. A `.glb` row has no context menu at all
  today (`asset_rules.cpp:33`), so rename alone takes it from unreachable to renameable — that is
  not incoherent, it is incremental. Deletion is irreversible where a rename is a rewrite, so the
  two do not carry the same risk. And it has an undesigned correctness gap: `planDeletion` today
  correctly **blocks** on a `kDocumentSkeleton` edge when another source's document binds the
  `.bskel` being deleted (`libs/assetlib/src/asset_refs.cpp:508-519`), which is what
  `docs/asset_containers.md:106` promises — "deleting a source never takes another source's rig with
  it". A grouped sweep that pushed a produced `.bskel` into an unconditional bucket would silently
  break that. Designing that check is its own piece of work. *Rejected: shipping the sweep now and
  testing the shared-rig case inside it, because the failure is a silent deletion of a rig a second
  model still needs.*

## Non-goals

- **Import settings are not surfaced.** Hiding the `.bimport` removes the only row that named one;
  nothing displays a `.bimport`'s contents today either, so nothing is lost, but re-editing an
  import after the fact stays out of reach. Unity's "rename the clip in the importer" answer needs
  this and is therefore also out.
- **No sub-asset rows.** A source row does not expand. See ADR-5.
- **The Level Editor viewport still accepts no drops.** It overrides no drag handler today, and
  placing a mesh in a level is not this feature.
- **No change to what is on disk.** No file moves between halves, no container's schema changes, no
  bake token moves.
- **`assetlib_cli` is not touched.** It addresses files by key and has no browser.
- **`Derived/BakedTextures/` stays as it is.** Hash-named, swept by `texture_prune`, never a row
  before or after.
- **Nothing in this feature deletes a model.** Per ADR-8. Combined with ADR-6, that leaves the
  browser with **no way to remove an imported model at all**: the `.bmesh` row that carries Delete
  today becomes unreachable, and the `.glb` row does not gain it. This is a real gap, stated rather
  than hidden — `assetlib_cli` and the filesystem remain the way out until the grouped deletion is
  designed. Materials, environments and directories keep their Delete throughout; only imported
  models lose it.

## Acceptance

- `just test assetlib editor` green, including new cases pinning: a `.glb` rename moving the
  document and every output with each referrer rewritten, and refusing to orphan a `.bskel` another
  source binds; `AssetOperations` refusing a `kDerived` target; the resolver returning the `.bmesh`
  for a source, and nothing for a source whose document is missing or whose `outputs` name no mesh.
- **Eyes**, since a browser is a picture: opening the test project shows `Meshes/ Materials/
  Environments/ Levels/` at the top level with no `Derived/` and no `.bimport` anywhere, the `.glb`
  tiles carry rendered mesh thumbnails rather than shell icons, and dragging `Coyote.glb` onto the
  Animation panel loads the coyote with its clips listed.

## What the survey found

**The split, and who owns it.** One table, `libs/assetlib/include/assetlib/project_layout.h:28-51`.
`originOf`/`requireOrigin` (`:88-97`) decide and enforce which half a key belongs to.
`c_RequiredDirectories` (`:60-72`) is what `Project::Create` scaffolds and
`Project::IsRequiredDirectory` protects.

**What an import produces.** `ImportDocument` (`libs/assetlib/include/assetlib/import_document.h`)
carries `outputs` — "every container this source produced, as mount keys, sorted" (`:49`) — and,
separately, `textureDir`, "where they went (empty when none)" (`:37-39`). **Textures are not in
`outputs`.** `importDocumentKeyFor`/`importedSourceKeyFor` (`:60-69`) convert between the source key
and the document key in both directions, and `loadImportDocument` has a host-path overload (`:74-76`)
that needs no mount — which is what a drop handler and a paint both have.

**The group is already a library concept.** `isGeometryContainer`
(`libs/assetlib/include/assetlib/asset_refs.h:30-31`) — "the three containers a mesh import
produces together. They travel as a group everywhere: regenerated as one, produced as one, counted
as one." `RefKind::kImportedSource`, `kDocumentSkeleton` and `kDocumentOutput` (`:51-53`) are
already edges.

**A `.glb` is not an asset type.** `assetTypeFromExtension`
(`libs/assetlib/src/asset_refs.cpp:253-267`) returns nullopt for it, so `planDeletion` (`:498-502`)
and `planRename` throw on one, and `editor::AssetAt`
(`apps/editor/src/Windows/ContentExplorer/asset_rules.cpp:33`) returns empty — which is why a `.glb`
row has no context menu at all today.

**`planRename` already refuses a half-move.** `libs/assetlib/src/asset_rename.cpp:184-190` refuses to
rename a `.bimport` alone because "its source key is derived from its own path, so a lone rename
would orphan the source", and points at renaming the directory instead. `RenamePlan`
(`asset_refs.h:300-321`) holds one `from`/`to` pair plus the `referrers` to rewrite — `referrers`
rewrites *stored references*, it does not express "a sibling file moves too", so ADR-7 needs a new
field rather than a reuse of that one.

**`DeletionPlan` already has the buckets.** `derived` (files swept with the target), `producers` (the
`.bimport` documents whose `outputs` must be rewritten, because an entry naming a missing file "reads
as **absent** to Reimport, which would put it straight back"), `contents`, `cascade`
(`asset_refs.h:194-240`).

**The explorer.** `ContentExplorerWindow::SetRootPath`
(`apps/editor/src/Windows/ContentExplorer/ContentExplorerWindow.cpp:140-150`) uses one `path` for
three jobs at once: `m_RootPath` (the data root `AssetAt` resolves against),
`m_Operations->SetDataRoot`, and both views' `setRootPath`/`setRootIndex`. Splitting those is ADR-3's
whole implementation. `HideBuildProductRows` (`:214-241`) hides rows per view via `setRowHidden`,
driven from `rowsInserted` on both models (`:70-83`), the tree's `expanded` (`:251-253`) and each
grid re-root (`:156-159`); it consults `editor::IsHiddenBuildProductFile`
(`apps/editor/src/util/asset_paths.cpp:34-38`), which today matches `.bvat` alone.

**Drops.** No custom MIME type anywhere; the views are `DragOnly` and
`QFileSystemModel::mimeData()` emits `text/uri-list`. Every target matches on extension through
`editor::FirstLocalFileWithSuffix` (`apps/editor/src/util/mime_files.h:13-14`):
`MaterialPreviewWindow` and `AnimationPreviewWindow` take `.bmesh` or `.benv`,
`AnimationEditorWindow` takes `.bmesh` (`AnimationEditorWindow.cpp:290-313`), `MaterialGraphView`
takes a `.ktx2` (`MaterialGraphView.cpp:16-31, 68-102`) and emits `TextureDropped`.
`ContentExplorerWindow` itself takes a `.glb`/`.hdr` from outside for import.

**Texture nodes have no picker.** `TextureNode::SetTexturePath` has three callers, and only one of
them is a gesture: the canvas drop (`MaterialEditorWindow.cpp:542`). The other two are machinery —
`BuildImportedMaterialGraph` wiring a graph from a glTF's own PBR material at import time
(`material_graph.cpp:257`), and `TextureNode::load` restoring a saved graph
(`nodes/TextureNode.cpp:154-157`). So dragging a `.ktx2` out of the browser is the only way a
*person* puts a texture in a graph — which is why ADR-1 cannot land without ADR-5.

**Thumbnails.** `AssetThumbnailCache` renders "a `.bmesh` as itself, a `.bmaterial` on a sphere"
(`apps/editor/src/Thumbnails/AssetThumbnailCache.h:36-37`) and, being a `StampedPixmapCache`, decides
staleness on the requested path's mtime. So a `.glb` row must request its resolved **`.bmesh`** path,
not its own: asking for the `.glb` would render nothing and would stamp a file a re-bake never
touches.

**`AssetOperations` has no per-extension branches.** `Delete`, `DeleteCascade`, `Rename` and `Bake`
are generic over a data-root-relative path and go straight through `assetlib`, so ADR-6 is an added
guard rather than deleted code.

## What changes

| Where | What |
|---|---|
| `libs/assetlib/include/assetlib/asset_refs.h`, `src/asset_rename.cpp` | `planRename` accepts an imported source; `RenamePlan` gains `source` and `outputs` |
| `apps/editor/src/util/import_outputs.{h,cpp}` (new) | source → `.bmesh` and source → its texture directory, cached against the document's mtime |
| `apps/editor/src/util/asset_paths.{h,cpp}` | the hide rule grows the `.bimport` sidecar |
| `apps/editor/src/Windows/ContentExplorer/ContentExplorerWindow.{h,cpp}` | browse root split from data root |
| `apps/editor/src/Windows/ContentExplorer/AssetFileModel.cpp` | a `.glb` row asks for its `.bmesh`'s thumbnail |
| `apps/editor/src/Windows/ContentExplorer/AssetOperations.cpp` | refuse a `kDerived` target |
| `apps/editor/src/Windows/ContentExplorer/asset_rules.cpp` | `AssetAt` returns an imported source |
| `AnimationEditorWindow.cpp`, `AnimationPreviewWindow.cpp`, `MaterialPreviewWindow.cpp` | accept a source alongside a `.bmesh` |
| `MaterialGraphView.cpp`, `MaterialEditorWindow.cpp` | accept a source and offer its textures |
| `docs/asset_containers.md`, `docs/assetlib_api.md`, `apps/editor/CLAUDE.md`, `ROADMAP.md` | the group rename rule, and what the explorer shows |

**What could break.** The paint path: resolving a source means reading a `.bimport`, and `data()` is
called on every paint — the resolver must cache, keyed on the document's mtime, or the grid stalls.
The rename group: a `.bskel` shared by a second source that binds to it (`ImportDocument::skeleton`
may name a rig another source produced, per `docs/asset_containers.md:101-107`) must be rewritten in
that second document, not orphaned — this is the same hazard that keeps grouped *deletion* out of
the feature under ADR-8, and it is survivable here only because a rename rewrites where a deletion
destroys. And `ContentExplorerWindow` accepts a `.glb` drop for import, so dragging a project's
own `.glb` back onto the browser must not attempt a re-import — `ImportMesh` refuses to overwrite,
so it fails safe, but it should not be offered.

## Tasks

1. **`feat(assetlib): a rename moves an imported source with everything it produced`** — `planRename`
   accepts a `.glb`; `RenamePlan` gains **two** buckets of `{from, to}` pairs, not one — `source`
   for the authored `.glb` and `outputs` for the containers the document names, re-stemmed, with
   every referrer of any of them rewritten. They are separate because their contracts differ: a
   rename that cannot move the source **fails** (nothing regenerates a `.glb`; `Reimport` reads
   *from* it), where a missing output is **skipped** (it is cache, and the document names the new
   path either way). One undifferentiated bucket was written first and rejected in review — the
   executing code then has to choose uniform treatment for a non-uniform list, and chose the cache
   rule for the one entry that is not cache. `DeletionPlan`'s `derived`/`producers`/`contents`/
   `cascade` set the precedent that each bucket says what it is for. Gate: new cases in
   `libs/assetlib/tests/src/AssetRename_test.cpp` pinning that the source, the document and every
   output move together; that a `.bskel` bound by a *second* source's document is rewritten there
   rather than orphaned; that a source with no rig (only a `.bmesh` in `outputs`) moves cleanly; and
   that `planRename` still refuses a lone `.bimport`.

2. **`feat(editor): resolve an imported source to what it produced`** — `editor::import_outputs`:
   the `.bmesh` for a source, the texture directory for a source, read through
   `loadImportDocument`'s host-path overload and cached on the document's mtime. Nothing calls it
   yet; the tests do. Gate: `just run editor_tests -- "[importoutputs]"`, pinning the resolution, an
   absent document, an `outputs` naming no mesh, and an empty `textureDir`.

3. **`feat(editor): the Content Explorer shows only what was authored`** — browse root at
   `Data/Authored` with the data root left at `Data`, the `.bimport` hidden, `.glb` rows carrying the
   mesh thumbnail through task 2, `AssetAt` returning an imported source, `AssetOperations` refusing
   a `kDerived` target. Gate: `just run editor_tests -- "[contentexplorer][assetrules]"`, plus eyes.

   This task also converges the editor's spellings of *is this path inside that root*. There are
   three besides `IsContainedRelativePath`: `AssetAt` and `IsHeldOpen`
   (`Windows/ContentExplorer/asset_rules.cpp:21-25, 56`) and the directory bookkeeping in
   `ContentExplorerWindow.cpp:472, 485-487`. They disagree — `AssetAt` rejects on a bare
   `startsWith("..")`, so a folder legitimately named `..hidden` is unactionable — and this task
   rewrites the rooting all three sit on, so it is where they become one call.

4. **`feat(editor): a viewport takes the source, not the container it produced`** — the three drop
   handlers accept a `.glb` and resolve it. Gate: `just run editor_tests -- "[drop]"` — the drop
   *rules* are driven straight through the handlers, since a `Drop` event cannot be synthesized.

5. **`feat(editor): a source dropped on the graph canvas offers its textures`** — `MaterialGraphView`
   accepts a `.glb`, `MaterialEditorWindow` presents that source's textures and makes the node.
   Gate: `just run editor_tests -- "[materialgraph]"`, plus eyes.

6. **`feat(editor): the source row renames what it produced`** — the context menu's Rename wired onto
   task 1. Gate: the rule driven through a free function, since every dialog here is modal; plus
   eyes.
