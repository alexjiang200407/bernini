# remove-non-project-assetlib — implementation plan

## Context

`assetlib` maintains two ways to do the same work, and the CLI is where both are exposed.

The sharp one is the importer. `assetlib::bake`
([libs/assetlib/src/bmesh_io.cpp:344](../../libs/assetlib/src/bmesh_io.cpp)) writes a glTF flat into
one `-o` directory and says so in a comment — *"a baked directory is its own data root"* — while
`editor::WriteImported*`
([apps/editor/src/Import/import_writers.cpp](../../apps/editor/src/Import/import_writers.cpp))
writes the same import into a project's `Meshes/`, `Textures/`, `Materials/`, `Skeletons/`,
`Animations/`, with rollback. Nothing but `assetlib_cli bake` calls the first.
[docs/asset_standards.md:55](../asset_standards.md) already records the cost: *"both importers — the
editor's and `assetlib_cli bake` — call `generateTangents`"*.

Above that, `envmap` and `describe` each carry two code paths inside one command — loose files or a
project for the first, a store or no store for the second — and five more commands address bare host
files with no project at all.

Underneath all of it, the type that knows what a project *is* — `Project`, with `.berniniproject`,
`Data/` and `c_RequiredDirectories` — lives in `apps/editor/src/Project/`, so `assetlib` cannot name
the concept its commands claim to serve. That is why "project-based" is currently a naming
convention rather than something the code checks: any directory passed to `-d` is accepted.

## Decisions

- **ADR-1 — The CLI has one entrypoint: `--project <file.berniniproject>`.** Every command opens a
  project; no command takes a data root. *Rejected: keeping `-d/--data-root`, because it unifies the
  flag spelling without making a project something the code knows — any directory would still pass,
  which is the failure the current surface makes invisible.*

- **ADR-2 — `Project` moves from `apps/editor/src/Project/` into `assetlib`.** It is already Qt-free
  and `assetlib` already links `nlohmann_json`, so the move is mechanical. After this change its
  consumers are the editor *and* `assetlib_cli`, and `assetlib` is the lowest layer both actually
  reach. *Rejected: restating the layout in the CLI, because `c_RequiredDirectories` is the one place
  the editor reads its own layout from and a second copy is how the two come to disagree.*

- **ADR-3 — `assetlib::bake` is deleted; the project-layout import writers move down into
  `assetlib`.** `WriteImportedMesh`, `WriteImportedRig`, `WriteImportedClips`,
  `FindMatchingSkeleton` and `RollBackImport` become `assetlib`'s, and the CLI and the editor import
  through them. *Rejected: wrapping `assetlib::bake` so the CLI places its outputs into project
  directories afterwards, because both importers would survive and drift.*

- **ADR-4 — Material authoring stays in the editor, and `assetlib_cli bake` writes no materials.**
  `material_graph.h:83` makes the board the single source of a material's routes — *"CompileMaterial
  reads the routes back out of it, so this is the only place that decides what a glTF material routes
  where, and there is no second table to disagree with"* — and the board is 3,600 lines of QtNodes
  that `scripts/install.py` deliberately keeps out of `assetlib_cli`. This is not a regression: the
  CLI writes no materials today either
  ([bmesh_io.h:170](../../libs/assetlib/include/assetlib/bmesh_io.h)), and the mesh lands with its
  submeshes unassigned, which both runtimes render unlit. *Rejected: moving the routing table into
  `assetlib` and making the graph a view of it — the only option that makes the CLI fully equal to
  the editor, but it reverses the ADR above and re-opens the material editor's model inside a feature
  that is already four slices. Also rejected: deriving graph-less materials in `assetlib`, which
  creates exactly the second routing table that ADR forbids.*

- **ADR-9 — The project layout is one table in `assetlib`, and every default that names a directory
  reads it.** `assetlib/project_layout.h` holds the ten category names and `c_RequiredDirectories`
  with no `AssetStore` dependency. Raised in review on #420: `assetlib` was already naming seven of
  these ten across `env_import.h`, `env_bake.h`, `texture_prune.h`, `material_bake.h`,
  `pak_pack.cpp` and `cli/main.cpp` — `"Textures"` four times, `"textures_src"` twice — none aware of the others, and
  ADR-2 would otherwise have added a tenth rather than collapsing them. *Rejected: leaving the
  scatter, because the layout is what a reference **means** — a `.bmesh` naming `Textures/skin.ktx2`
  and the bake that writes that file have to agree, and they are written in different translation
  units. Also rejected: keeping the constants on `Project`, because `material_bake` needs a directory
  name and has no business pulling a project and its mount in behind it.*

- **ADR-10 — `assetlib` gets its own host-path-to-mount-key conversion (`mountKeyFor`, internal to
  `src/`), and the editor's Qt `Rebase` stays.** The two are three lines of
  `std::filesystem::relative(...).generic_string()` apiece. *Rejected: making `Rebase` call
  `mountKeyFor`, because `Rebase` also does the opposite direction (join then `weakly_canonical`) and
  returns its input unchanged on error, and the material graph's saved JSON depends on both — a
  behavioural change there is not worth folding into a move. Recorded so the duplication is a choice
  rather than an oversight; the editor half disappears if the material graph ever stops storing
  paths as `QString`.*

- **ADR-11 — `list` is the one command that takes no project.** Its subject is a `.bpak`, which is
  what a project *produces* rather than something inside one, and `PakFile` reads a standalone
  archive. *Rejected: requiring `--project` and ignoring it, which would make the flag a ritual;
  and defaulting the archive to the project's own, which invents surface for a debugging command.
  This does not reopen the two-entrypoint problem — that was two ways to do the same work, and
  nothing else inspects an archive.*

- **ADR-5 — An asset argument is a mount key through the project's store; an output that is not a
  project asset stays a `std::filesystem::path`.** `obj`'s `.obj` dump, `pack`'s `.bpak` and
  `strip`'s shipping copy are host paths; everything read from or written into the project is a key.
  This is [STYLE.md](../../STYLE.md)'s Paths split applied to the command line. *Rejected: forbidding
  every write outside the project, because it folds a stripping policy into `pack` — a separate
  decision.*

- **ADR-6 — `envmap` loses `-o`, `-c` and `-i`.** Its outputs are project assets and belong in
  `Sky/`, `EnvLighting/`, `Environments/` and `textures_src/`, which is what `--project` already
  does. *Rejected: keeping them as a debug path, because that is precisely the dual-mode command this
  feature exists to remove.*

- **ADR-7 — `assets/` becomes a project:** `assets/Test.berniniproject` over `assets/Data/`.
  `golden/` (test images), `Frozen/` (schema fixtures) and the loose `.glb` sources are not project
  assets and stay where they are. *Rejected: leaving `assets/` flat and calling it a data root,
  because the repo's own asset tree would be the one thing the new entrypoint cannot open.*

- **ADR-8 — `--project` is always explicit; no upward discovery from the working directory.**
  [scripts/install.py:4](../../scripts/install.py) documents `assetlib_cli` as the binary that "reads
  nothing relative to its working directory", and that property is what makes it safe to put on
  PATH. *Rejected: walking up from the cwd for the nearest `.berniniproject`, which is convenient and
  quietly makes the tool's behaviour depend on where it was invoked.*

## Non-goals

- The path-taking free functions (`load`, `save`, `loadMaterial`, `saveVat`, …) stay. `AssetStore` is
  built on them and the editor writes through them; they are the primitive, not an entrypoint.
- No container format or schema version changes, so nothing needs re-baking.
- No change to `gamelib`'s or `bgl`'s runtime load path.
- `assets/golden/` and `assets/Frozen/` do not move, and neither do the loose `.glb` sources.
- The editor's import dialog and its material graph are untouched; only *where the writers live*
  changes, and `WriteImportedMaterials` stays behind.
- `pack` does not gain stripping.
- `bernini-test-project` is not re-baked here.
- The latent bug at `MaterialEditorWindow.cpp:826` — a graph-less material's board is seeded with
  factors only, though the comment claims its texture references are rebuilt — is out of scope and
  recorded here so it is not mistaken for this feature's doing.

## Acceptance

- A new `assetlib_tests` case creates a project, imports `assets/suzanne.glb` through the moved
  writers, and asserts the exact file set that lands in `Meshes/`, `Textures/`, `Skeletons/` and
  `Animations/` — no `Materials/`, per ADR-4 — then describes, refs and packs it. This is what pins
  "one importer".
- `just test` stays green after `assets/` moves under `assets/Data/`: every golden image in
  `assets/golden` still matches, which proves the layout move broke no runtime path.

## What the survey found

**The CLI surface today** ([libs/assetlib/cli/main.cpp](../../libs/assetlib/cli/main.cpp), 1088
lines). Thirteen subcommands in three groups:

| group | commands |
|---|---|
| require a data root | `bakevat` `refs` `prune` `pack` `migrate` (`-d`, or positional for `migrate`) |
| dual-mode | `envmap` (`-o/-c/-i` loose **or** `-p` project), `describe` (`-d` optional; with it `AssetStore::Describe` stats every route and reports a stale bake, without it `assetlib::describe` reports only what the container records) |
| bare host path | `bake` `obj` `tangents` `strip` `list` `exposure` |

**`Project`** ([apps/editor/src/Project/Project.h](../../apps/editor/src/Project/Project.h)) is
Qt-free — `<core/file/LooseFileSystem.h>` and `<nlohmann/json.hpp>` only. It owns
`c_FileExtension`, the ten `c_RequiredDirectories`, `Create`, `Open`, `Save`, `IsRequiredDirectory`,
`GetDataDirectory` and a held `AssetStore`. Fourteen files include it: ten in `apps/editor/src`, four
in `apps/editor/tests/src`.

**`import_writers.cpp`** (242 lines) is Qt-light except for materials. `WriteImportedRig`,
`WriteImportedMesh`, `WriteImportedClips`, `FindMatchingSkeleton` and `RollBackImport` touch Qt only
through `Rebase(QString, dir, toRelative)` ([material_graph.h:28](../../apps/editor/src/Windows/MaterialEditor/material_graph.h)),
and only to compute the *reference a container stores* — `mesh.skeleton`, `clips.skeleton`, the
material path `attachMaterial` takes — which is a mount key `assetlib` already produces natively via
`normalizeRef` ([libs/assetlib/src/ref_paths.h](../../libs/assetlib/src/ref_paths.h)). The writers'
own destinations stay `std::filesystem::path`: `save`, `saveSkeleton` and `saveAnimations` all take
one, `RollBackImport` calls `fs::remove`, and `core::file::IFileSystem` has no write or delete method
to route either through. So it is the stored reference that becomes a key, not the disk target.
`WriteImportedMaterials` is the exception and stays: it constructs a
`MaterialGraphModel`, calls `BuildImportedMaterialGraph`, and compiles routes back out of the board.
`import_pipeline.cpp` (315 lines) is the Qt shell above them — dialogs, `BackgroundTask`, message
boxes — and does not move.

**`assets/`** is already nine tenths of a data root: `Meshes/ Materials/ Textures/ Sky/
Environments/ EnvLighting/`, missing `Skeletons/ Animations/ Levels/ textures_src/`. Of the 248
string references to `assets/` in the tree, 206 are `golden/` and 22 are `Frozen/`; only ~17 address
project assets. The root [CMakeLists.txt:30](../../CMakeLists.txt) copies the whole directory to the
runtime output, so the move needs no build change.

**Nothing outside the repo drives the CLI.** No CI workflow and no script invokes it;
`scripts/install.py` only stages it onto PATH.

## What changes

| where | what |
|---|---|
| `libs/assetlib/include/assetlib/Project.h`, `src/Project.cpp` | new home for `Project`, in `namespace assetlib` |
| `libs/assetlib/include/assetlib/asset_import.h`, `src/asset_import.cpp` | new home for the five project-layout writers; the references they store become mount keys |
| `libs/assetlib/include/assetlib/bmesh_io.h`, `src/bmesh_io.cpp` | `bake` deleted; `writeTextures` and `toBMesh` stay |
| `libs/assetlib/cli/main.cpp` | every command opens a `Project`; the dual modes and the bare-path arguments go |
| `apps/editor/src/Project/` | removed; fourteen files re-include and requalify |
| `apps/editor/src/Import/import_writers.*` | keeps `WriteImportedMaterials`; the rest forwards to `assetlib` |
| `assets/` | `Test.berniniproject` + `Data/`; ~17 test path references follow |
| `docs/asset_standards.md`, `asset_schema.md`, `envmaps.md`, `vat.md`, `archives.md`, `CLAUDE.md`, `scripts/install.py` | the CLI cookbook and every `-d`/`-o` example |

**What could break.** The `assets/` move touches the widest surface — a missed path reference fails
loudly as a container that will not load, not silently. The `Project` move is the riskiest for review
volume rather than for correctness: fourteen files change and none of them change behaviour, so the
diff must be provably a pure move. Deleting `envmap`'s loose outputs is the only user-visible
capability removed.

## The tasks in order

1. `refactor(assetlib): move Project down from the editor` — `Project` into `assetlib`; the fourteen
   includers requalify; `Project_test.cpp` moves from `editor_tests` to `assetlib_tests`. Pure move,
   no behaviour change. **Gate:** `just test assetlib editor` — `Project_test` passes in its new home
   and the editor suite is unchanged.

2. `refactor(assetlib): one table for the project layout` — `project_layout.h`; `Project`'s
   constants move to namespace scope, and `env_import`'s four directory defaults, `env_bake`'s and
   `texture_prune`'s and `material_bake`'s `textureDir`, and `pak_pack`'s `c_AuthoringDir` all read
   from it. Places that know where a texture lives: 4 -> 1. **Gate:** `just test` — `Pack_test`'s
   authoring-source exclusion and `MaterialBake_test`'s output paths both run off these defaults, so
   a wrong constant fails rather than merely reads oddly.

3. `refactor(assetlib): move the project import writers down from the editor` — the five writers into
   `assetlib::`, camelBack per the library's `.clang-tidy`, storing mount keys via a new
   `mountKeyFor` instead of `Rebase`d host paths; `WriteImportedMaterials` stays in the editor.
   **Gate:** the nine `[importedrig]`/`[importedmesh]` cases move with the code into
   `assetlib_tests` and still pass — they already assert the stored reference is
   `Skeletons/unit.bskel`, which is exactly the property `Rebase` used to provide — plus one new
   assertion that the ambiguous-rig message names both rigs as keys rather than host paths.

4. `feat(assets): assets/ becomes a project` — `assets/Test.berniniproject` over `assets/Data/`, the
   six category directories moved beneath it and the four missing ones scaffolded; `golden/`,
   `Frozen/` and the `.glb` sources stay put; ~17 path references updated. **Gate:** `just test` —
   every golden image in `assets/golden` still matches.

5. `feat(assetlib): assetlib_cli takes a project, and only a project` — `--project
   <file.berniniproject>` on all thirteen commands, asset arguments become mount keys, `envmap` loses
   `-o/-c/-i`, `describe` loses its store-less path, `refs`/`prune`/`pack`/`migrate`/`bakevat` swap
   `-d` for `--project`. `bake` unchanged in this task. **Gate:** `assetlib_tests` covers
   `AssetStore::ResolveWritePath`, the one conversion standing between a typed key and a write --
   no suite runs the CLI binary, so the commands themselves are exercised by hand against
   `assets/Test.berniniproject` and the transcript goes in the PR body.

6. `feat(assetlib): one importer` — `bake` imports into the project through task 3's writers;
   `assetlib::bake` deleted. **Gate:** the acceptance round-trip — import `assets/suzanne.glb` into a
   fresh project, assert the file set, then `describe`, `refs` and `pack` it.

Each task carries the doc changes it makes false, per
[bcp-implement § 7](../../.claude/skills/bcp-implement/SKILL.md); task 5 carries the bulk of
`docs/asset_standards.md`'s CLI cookbook.
