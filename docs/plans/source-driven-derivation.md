# source-driven-derivation — implementation plan

## Context

A project's derived containers can be *re-cooked* but not *produced*. Every path that makes one
current is keyed on the file already existing: `LoadRegenMesh` peeks the key of the `.bmesh` it was
handed ([AssetStore_Regen.cpp:267](../../libs/assetlib/src/AssetStore_Regen.cpp)), and `Migrate`
walks `recursive_directory_iterator(GetDataRoot())` and re-saves what it finds
([migrate.cpp:84](../../libs/assetlib/src/migrate.cpp)). Delete a `.bmesh` and nothing puts it back.

That is what stops derived data being gitignored, which is the direction this pipeline has been
moving since #463 split authored state from derived state. It also hides a live bug.

`ImportDocument` records no rig, so a mesh's `.bskel` is *derived* from the source key:
`groupSkeletonKey` scans `Skeletons/` for a rig whose header names this source
([AssetStore_Regen.cpp:121](../../libs/assetlib/src/AssetStore_Regen.cpp)). One source, one rig — a
shared rig is unrepresentable. So `WriteImportedRig` writes a `.bskel` unconditionally
([asset_import.cpp:337](../../libs/assetlib/src/asset_import.cpp)), and importing a second `.glb`
with the mesh on forks a signature-matching duplicate. That duplicate then makes
`FindMatchingSkeleton` throw for **every later clips-only import**, because two rigs match and which
one the clips attach to would depend on directory order. The shared-humanoid-skeleton case — a.glb
carrying the rig, b.glb a second mesh on it — cannot be authored at all today.

The two are one problem: removing the fork without giving the document a rig field breaks
regeneration for the sharing mesh, because `groupSkeletonKey` will not find a rig that names another
source.

## Decisions

- **ADR-1 — `ImportDocument` gains `skeleton`: the `.bskel` mount key this source's joints
  address.** The binding becomes authored rather than inferred, which is what lets one rig serve
  many sources. *Rejected: de-duplicating at write time and keeping the scan — the scan is what
  makes a shared rig unrepresentable, so a mesh that reused another source's rig would then fail to
  regenerate.*
- **ADR-2 — `ImportDocument` gains `outputs`: the mount keys this source produced.** A project's
  derived set becomes answerable from the authored side alone, which is the only way to produce a
  file that does not exist yet. *Rejected: deriving it from a data-root walk, which is what
  `Migrate` does and is exactly the thing that cannot see an absent file.*
- **ADR-3 — an import reuses a signature-matching rig; it writes a new `.bskel` only when none
  matches.** *Rejected: leaving the fork and de-duplicating in `migrate` afterwards — the duplicate
  poisons `FindMatchingSkeleton` the moment it is written, so every clips-only import between the
  fork and the migrate throws.*
- **ADR-4 — the document is authoritative for the rig, and `groupSkeletonKey` is deleted.**
  `migrate` backfills `skeleton` for documents written before the field; a skinned source whose
  document lacks it refuses and names `migrate`. *Rejected: falling back to the scan when the field
  is empty — two ways to answer one question, and the scan's answer is wrong for precisely the
  shared-rig case the field exists to allow.*
- **ADR-5 — `AssetStore::Reimport` produces a source's outputs, and `migrate` runs it before its
  walk.** Source-driven: enumerate `meshes_src/*.bimport` and write the outputs the document names.
  *Rejected: teaching `Migrate`'s walk to notice absences — a walk over derived files has nothing to
  enumerate on a project that has none.*

  *Written as "missing or stale"; narrowed to **absent only** during implementation.* A stale
  container on disk is `Migrate`'s: it can read and re-save one, which is cheaper than a re-import
  and is the operation that already existed. Overlapping them made `Migrate` report one problem
  twice — a group whose source had gone came back as four failures where three were expected, one
  per file plus one for the source — which is what surfaced it.
- **ADR-8 — a re-import reproduces the writer that produced the outputs, and `outputs` says which
  one that was.** A source that produced a `.bmesh` swept that mesh for its clip set's posed boxes,
  because `WriteImportedRig` had it in hand and not yet on disk; a clips-only source had no mesh and
  swept the project's, through `bakeBoundsForRig`. Following the same split makes a re-imported
  container byte-identical to an imported one, which is the only definition of correct that can be
  tested. *Rejected: always sweeping project-wide, which is the more general measurement and
  produced a `.banim` that differed from the import's while agreeing on every box a loader looks
  up — an unexplainable diff in a container that is supposed to be reproducible. Re-measuring across
  the project is `RebakePosedBounds`, which is already its own operation.*
- **ADR-6 — `LoadRegen*` stays as the seam `pack`, `migrate` and `vat_bake` use; a load refuses a
  stale container instead of re-cooking it.** The cost of that re-cook recurs on every load and is
  never written back. *Rejected: removing the seam outright — `pack` has to make an archive current
  with no disk to write to, and that is a legitimate regeneration.*

  *The refusal sits at the gamelib call site, not inside the seam*, which the survey forced:
  `LoadRegen*` does two jobs at load, regenerating stale geometry **and** applying the import
  document's bindings over the result. Only the first is being given up — a rebind must still reach
  a mesh without a re-cook — so the guard goes in front of the call and the seam is unchanged.
- **ADR-9 — the editor offers to rebuild as a project opens.** The editor renders through
  `game::AssetManager`, so the refusal reaches its viewport: without a recovery path this would
  trade a silent re-cook for a project that cannot be opened. `AssetStore::StaleGeometry` is the
  scan behind the offer -- header peeks and one document hash apiece. *Rejected: `Migrate(dryRun)`
  as the scan, which re-cooks every stale group in memory to answer a yes/no question a project
  open cannot afford to ask.*
- **ADR-12 — only the scene load refuses; the editor's inspection surfaces keep regenerating.**
  `RequireCurrent` sits in `game::AssetManager`, so the main viewport refuses. The editor's other
  readers — `LoadMeshThroughSeam` behind thumbnails, the material preview and the material editor,
  and the animation preview's direct `LoadRegen*` calls — go to `AssetStore` and still re-cook in
  memory. Deliberate: those exist to *show you the project*, and a user who declined the update
  offer needs to see their assets in order to decide, where a scene load has no such excuse. The
  cost argument does not reach them either — they read one asset on demand, not a scene's worth per
  load. *Rejected: refusing everywhere, which makes a stale project one that cannot even render a
  thumbnail of what is wrong with it.*
- **ADR-13 — the two new document fields are edges in the reference graph.** `skeleton` and
  `outputs` are references like any other, and the document is now the only thing that records
  them. *Rejected: leaving them out as "just bookkeeping" — an `outputs` entry naming a key a
  rename moved reads as **absent** to the producing side, so the next `migrate` would put the old
  file back under its old name. On a project that gitignores its derived tree that is silent data
  corruption from a routine rename.*
- **ADR-11 — a source's extracted textures are produced from the texture folder being absent or
  empty**, not from `outputs`. A `.ktx2` has nowhere to carry a header, so nothing can key one;
  `StaleImportedTextureSources` compares the *source's* stamp, which is unmoved on a fresh checkout
  and so reports nothing. Without this the commit rule below would be false for `Textures/`.
  *Rejected: listing extracted textures in `outputs` — the extract names its files after the source
  images it finds, so the list would be a second claim about what it produced, able to disagree with
  the extract itself.*
- **ADR-10 — the commit rule is written down here, and applied in `bernini-test-project` by hand.**
  Sources and authored documents committed, derived containers ignorable. The test project is a
  different repository, so this change cannot carry its `.gitignore`; what it carries is the
  ability to regenerate, which is what that `.gitignore` was waiting on. *Rejected: landing the two
  together — they cannot be one commit, and a `.gitignore` pushed before this merged would leave
  that project unopenable.*
- **ADR-7 — the in-repo `assets/` tree is not gitignored.** It is a fixture tree, not a project:
  `bgl_tests` opens `assets/Data` as a store
  ([TestEnvironment.cpp:12](../../libs/bgl/tests/src/util/TestEnvironment.cpp)),
  `AlphaTest_test.cpp:212` loads a baked `.ktx2` by its content-hashed name, and
  `GltfSkin_test.cpp:491` reads `assets/Data/Meshes/apples.bmesh` directly. *Rejected: gitignoring
  it for consistency with the rule — the rule is about projects, and these files are test inputs
  that no import in this repo produces.*

## Non-goals

- **No stable asset ids.** References stay paths; `asset_rename.cpp` stays. That is the change that
  makes a directory move free, and it is its own feature.
- **No `Src/` split.** Proposed and dropped; sources stay under `Data/`.
- **No texture or `.hdr` sidecar.** A `.ktx2` still carries no document of its own; `textureDir`
  and `textureStamp` on the mesh's `.bimport` stay the extracted textures' whole key.
- **No `bgl` change**, and no change to any container's binary layout except the two `.bimport`
  fields, which are authored JSON.
- **The `bernini-test-project` `.gitignore` is not in this diff** — different repository. This PR
  makes it *possible*; a box says what to run there.

## Acceptance

- A test that imports two sources carrying one rig and asserts **one** `.bskel` exists, the second
  mesh names it, and a clips-only import against that rig then succeeds — the case that throws
  today.
- A test that deletes every derived file from a project and asserts `Reimport` puts them back, byte
  for byte identical to what the import wrote.
- A test that a skinned `.bimport` without `skeleton` refuses with a message naming `migrate`, and
  that `migrate` backfills it.
- `TokenCanary_test` unmoved — no cache-entry layout changes in this diff.
- `just test` green across all suites.

## Commits

1. `docs(plans): plan source-driven derivation` — this file.
2. `feat(assetlib): an import document records the rig it binds and the outputs it produced` —
   the two fields, the codec, `migrate`'s backfill, `groupSkeletonKey` deleted.
   Gate: `just test assetlib`.
3. `fix(assetlib): an import binds a rig it matches instead of forking a duplicate` — ADR-3, and
   the clips-only import that follows it. Gate: the first acceptance test.
4. `feat(assetlib): produce a project's derived containers from its sources` — `Reimport`, and
   `migrate` running it first. Gate: the second acceptance test.
5. `refactor(gamelib,editor): a stale container refuses at load, and the editor offers to rebuild`
   — ADR-6 and ADR-9. Gate: `just test gamelib editor`.
