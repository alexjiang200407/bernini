# editor-refactor — implementation plan

## Context

`apps/editor/src/Windows/ContentExplorer/ContentExplorerWindow.cpp` is 1585 lines and its header 411.
It is five unrelated jobs behind one class name: the view plumbing (models, navigation, the empty
placeholder, build-product row hiding), the context menus and the naming/kind rules under them, the
on-disk asset operations (Delete, Delete Cascade, Rename, Add Directory, Bake), drag-and-drop
routing, and the whole glTF/HDR import pipeline. The last of those is already seven `static`s that
take everything they need and touch no member — free functions wearing a class prefix, and
`ImportedMaterials_test.cpp` and `ImportedRig_test.cpp` already call them as such.

`MaterialEditorWindow.cpp` is 1191 lines with a 200-line constructor that is pure widget assembly,
and three members — `m_MaterialGraphs`, `m_GraphForSubmesh`, `m_CurrentSubmesh` — that nearly every
method reaches into.

The cost is navigation: changing one thing means scrolling past four jobs that have nothing to do
with it, and the file name no longer predicts what is inside. Nothing is broken and nothing is
slow — this is the tax on every future edit to the two files the editor changes most.

## Decisions

- **ADR-1 — Cut along responsibility, so both the `.cpp` and the header shrink.** A responsibility
  that leaves takes its declarations with it, so `ContentExplorerWindow.h` stops being a 411-line
  table of contents for the subsystem. *Rejected: splitting the translation unit while keeping one
  class (`ContentExplorerWindow_Import.cpp`), which reviews as a trivially-verifiable pure move but
  leaves the header and the god-object exactly as they are — the file gets shorter and the thing you
  have to understand does not.*

- **ADR-2 — The import pipeline leaves the Content Explorer entirely, to `src/Import/`.** Importing
  a glTF is not a thing a file browser does; the browser is one caller of it, reached from a drop.
  The seven statics move as free functions and the two `ImportOutcome`-returning members follow them,
  taking their parent widget as a parameter. *Rejected: keeping them under
  `src/Windows/ContentExplorer/`, which preserves the false claim that the import belongs to a
  window.*

- **ADR-3 — Moved functions are renamed to fit their new home.**
  `ContentExplorerWindow::WriteImportedMaterials` becomes `editor::import::WriteMaterials`: the
  namespace already says "import", so the prefix stops earning its keep. *Rejected: keeping every
  name (`import::WriteImportedMesh`), which keeps `git log -S` on the old name working and makes the
  diff mechanically checkable, at the cost of a redundant prefix on every call site forever.*

- **ADR-4 — `MaterialEditorWindow`'s three entangled members become a `MaterialGraphSet` type.**
  They are one thing — the table of graphs and the submesh-to-graph map over it — and the lookups
  reading them (`CurrentGraph`, `FindGraphForPath`, `ResetGraph`, `ForgetMaterialsOnDisk`,
  `ReleasePreviewMaterials`) are the only code that has any business seeing their representation.
  Lifted out, they are also testable without a device, which the window itself is not. *Rejected:
  lifting only what does not need `this` (compile, build, tangents) and leaving the state where it
  is, which is lower-risk but lands the window at ~700 lines with the god-object intact.*

- **ADR-5 — The 200-line constructor's widget assembly moves to its own file.** It is the single
  largest block in the file, it is not behaviour, and it is not where anyone debugging the material
  editor needs to look. *Rejected: a `.ui` file, which is the repo's normal answer for layout but is
  a different change — this UI is built dynamically for a reason, and converting it is not a
  refactor.*

- **ADR-6 — Behaviour is unchanged, and the tests prove it by not changing.** Every assertion in
  `apps/editor/tests` stays as written; only `#include` lines and the spelling of a call move. A
  refactor that needed an assertion edited would be a behaviour change wearing a refactor's clothes.

## Non-goals

- **The other five files over 500 lines** — `AssetThumbnailCache.cpp` (836), `MainWindow.cpp` (694),
  `MaterialPreviewWindow.cpp` (573), `AnimationPreviewWindow.cpp` (536),
  `AnimationEditorWindow.cpp` (500). Same problem, deliberately a follow-up: this diff is already as
  large as one reviewer can read as a move.
- **New test cases.** Extraction incidentally makes rules testable that were stranded behind modal
  dialogs (see `apps/editor/CLAUDE.md` § What is testable). Pinning them is worth doing and is not
  this change — mixing new tests in would cost the reviewer the ability to read this as a pure move.
- **Any behaviour, UI or on-disk format change.** No new features, no bug fixes, no `.ui` edits.
- **A line-count ceiling on editor sources.** Considered as a mechanical gate and left out: it would
  be a rule invented by this change rather than agreed, and it converts the next honest 520-line file
  into a fake regression.
- **The test files over 400 lines** (`ContentExplorerWindow_test.cpp` at 553,
  `MaterialGraph_test.cpp` at 541). A test file's length is not the same problem.

## Acceptance

- `just test editor` green, with **no assertion in `apps/editor/tests` altered** — the diff there is
  confined to `#include` lines and the qualified name a call is spelled with. This is the gate: a
  behaviour change would have to move an assertion too.
- `just build` clean on the configured preset.
- `just format --check` and `just tidy --changed` clean over every file touched.
- `apps/editor/CLAUDE.md` still describes the tree that exists — the paths it names for the import
  statics and the modal-dialog coverage note both move.

## Commits

Bottom-up: what nothing depends on leaves first, so each commit compiles and passes with the ones
above it not yet written.

1. `docs(plans): plan the editor file split` — this document.
2. `refactor(editor): lift the Content Explorer's asset rules out of the window` —
   `AssetAt`, `IsMaterialAsset`, `IsValidAssetFileName` become `editor::` free functions in
   `Windows/ContentExplorer/asset_rules.{h,cpp}`.
   Gate: `just run editor_tests -- "[contentexplorer]"`.
3. `refactor(editor): move the import pipeline out of the Content Explorer` — `src/Import/`:
   `import_writers` (the four writers and `FindMatchingSkeleton`), `import_pipeline` (`ImportMesh`,
   `ImportEnvironment`, `RollBack`, `Outcome`, `Options`), `drop_import` (what a drop accepts and
   what it runs). The window's `dropEvent` becomes a call.
   Gate: `just run editor_tests -- "[importedmaterials],[importedrig],[importedmesh]"`.
4. `refactor(editor): give the Content Explorer's asset operations their own type` —
   `AssetOperations` owns Delete / Delete Cascade / Rename / Bake / Add Directory and the
   held-open guard, and reports back by signal so re-rooting a deleted or renamed folder stays
   the window's job.
   Gate: `just run editor_tests -- "[contentexplorer]"`.
5. `refactor(editor): lift the material editor's graph table into MaterialGraphSet` — the three
   entangled members and the lookups over them.
   Gate: `just run editor_tests -- "[materialeditor],[materialgraph]"`.
6. `refactor(editor): move the material editor's file operations to material_io` — `BuildMaterial`,
   `BakedTexturesSummary`, `IsAlreadyDefault`, `DefaultMaterialPath`, `GenerateTangents`.
   Gate: `just run editor_tests -- "[materialeditor],[materialgraph]"`.
7. `refactor(editor): move the material editor's widget assembly and graph compile out` — the
   constructor's layout into `material_editor_ui`, `CompileGraph` into `graph_compiler`.
   Gate: `just test editor`.
8. `docs(editor): follow the split` — `apps/editor/CLAUDE.md`.
   Gate: `just test editor`, `just format --check`, `just tidy --changed`.
