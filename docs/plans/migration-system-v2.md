# migration-system-v2 — stale geometry is re-imported from its source, not migrated

## Context

Parallel AI agents now each change asset import code in their own worktree. The shared test
project's binaries can only be baked at one branch's state, so debugging a second branch in the
editor means merging binaries — the merge this feature exists to make unnecessary.

#413 (the dog) is the case study. Every `.banim` schema was byte-identical before and after the
fix; the *values* were wrong, because the old importer dropped the armature node chain's channels
on the way to the file and never wrote the eye→bone parenting into the `.bmesh` at all. The schema
system ([docs/asset_schema.md](../asset_schema.md)) converts any layout, but no conversion, hook or
file→file transform can recover data a bug discarded before writing. Only the source `.glb` holds
the truth — and today the source is read in place, never copied, and no container records its path,
so after import it is unreachable. The dog was carried by a hand re-import, one week after the
schema landing needed a hand re-bake. That cost is now recurring, which is the trigger
[migrate-assets](#adr-7) was waiting on.

**Dependency:** this plan assumes `feat/remove-non-project-assetlib` (#418) lands first — one
project-based import path with `Project` down in `assetlib`, `assetlib::bake` deleted, `assets/`
itself a project, and materials authored only through the material editor. That refactor is this
feature's enabler: one import path means one place to copy the source and stamp the fields. The
survey below cites the pre-#418 tree; the tasks name where the code lives after it.

## Decisions

- **ADR-1 — Migration is re-bake from source.** The importer is always the current writer, so the
  chain-vs-collapse trap the retired migration spec documented never arises. This does not retire
  the schema system — the two split cleanly and each is the only one that can do its half. The
  schema owns *shape*: it is what reads the authored containers (`.bmaterial`, the env family),
  which have no source to re-import from; what lets the shipped game — whose `.bpak` carries no
  sources — read older files at all; and what opens the *old* geometry container so ADR-8 can
  salvage its attached state after a layout change. Re-import owns *meaning and derivation*: data
  the old file never contained. *Rejected: authored file→file data migrations (Laravel/Django
  style) — they provably could not have fixed #413, layout changes are already free via the
  schema, and meaning changes where the data survives are already `chunk::Hook`s. Also rejected:
  re-import as the only mechanism ("re-import whenever anything differs") — it cannot serve an
  authored container or a source-less store, and a pure layout change absorbed silently by the
  converter would instead cost a re-import per asset and an LFS rewrite of every binary.*
- **ADR-2 — Import copies the `.glb` into the project, and the geometry containers reference it.**
  A `meshes_src/` category on the `textures_src` pattern; `.bmesh`, `.bskel` and `.banim` each
  record the data-root-relative source path and its `SourceStamp`. Only a self-contained source
  can be one file copied and one hash stamped, so importing a `.gltf` — JSON with sidecar `.bin`
  and image files resolved relative to its directory (`drop_import.cpp:18` accepts both today) —
  is refused with "export as .glb". After #418 there is exactly one import path — the
  project-layout writers, down in `assetlib` — so the copy, the stamp and the refusal live in that
  single place and the CLI records the same fields as the editor because they run the same code
  (`assetlib::bake` and its "own data root" convention are gone with #418; the pre-#418 CLI help
  advertising `.glb/.gltf` at `cli/main.cpp:185-187` goes with them). Everything recorded is a
  mount key, project-relative, per #418's own rule. Editing the copied source is legitimate, not a
  corruption: its
  stamp mismatch stales the group (ADR-6) and the next load or `migrate` re-imports. The stamp is
  content, never mtime — #371 moved stamps *off* mtime because it false-stales; mtime survives
  only as `stampOf`'s memo key, so the hash costs little per load. *Rejected: recording the
  external path the import read from — it
  breaks the moment a project moves machines, which is precisely when a migration is wanted; and
  copying a `.gltf`'s sidecar set, which cannot be stamped as one content hash and turns "the
  source" into a directory contract.*
- **ADR-3 — Semantic staleness is an import-revision token, compared for inequality.** One
  `constexpr uint64_t` in `assetlib`, changed to a fresh random value on every semantic importer
  change (the dog fix is the model); containers store the token they were baked with; any mismatch
  means stale. There is no migrations directory and no stored history: the only persistent record
  is the token (and source reference) each container carries, because the "migration" code is the
  current importer itself. Laravel and Django must keep every step — their migrations already ran
  against databases they cannot regenerate; here the source *is* the database, and the retired
  spec showed a kept chain is unsound in this domain anyway (every step writes current, so
  replayed hops silently skip each other's fixes). *Rejected: a monotonic counter compared by `<`
  — two branches that both bump to the same number are indistinguishable, the same two-branch
  trap that disqualified version numbers from driving layout. Also rejected: schema-diff alone —
  #413's schema was identical.*
- **ADR-4 — The editor re-bakes stale geometry at load, in memory only; `assetlib_cli migrate`
  is the writing path.** The in-memory seam is *new* plumbing — today's only load-time re-bake,
  `EnsureVatBaked`, dumps its bytes to disk, and keeps doing so: `.bvat` is git-ignored derived
  output, so writing it is legitimate. The geometry containers are version-controlled, which is
  what forbids the same move for them — and their loads already materialize in-memory structs
  (`LoadMesh` returns a `BMesh`), so the seam changes which bytes those structs come from, not how
  they are represented. Matches the existing rule "load in memory; write current on explicit
  save". Consequence for VAT: `VatFreshness` compares the `.bvat`'s stored stamps against the
  *disk* files, so with stale geometry re-baked only in memory it would call a pre-change `.bvat`
  fresh and the VAT tier would render old geometry beside a re-baked skinned tier. So `.bvat`
  freshness gains the token axis — a `.bvat` whose geometry group is stale is itself stale — and
  `EnsureVatBaked` re-bakes from the seam's re-baked inputs (it already reads them through the
  store) and still writes disk. *Rejected: writing the geometry at load, which dirties
  version-controlled binaries; and leaving VAT freshness stamp-only, which splits the two tiers.*
- **ADR-5 — A stale container in a writable store must be re-bakeable, or it refuses to load.**
  Freshness is the token plus, when a source is recorded, its content hash. A *current* container
  (token matches; recorded source, if any, stamps clean) loads; a stale one whose source is
  missing or was never recorded refuses, surfaced like today's unreadable-container errors — a
  stale one whose source is present re-bakes per ADR-4. Every editor import records its source
  (ADR-2), so editor-authored assets are always re-bakeable; what this refinement permits is a
  container written programmatically by the *current* writers — the shape every synthetic test
  fixture has (`gamelib`'s `RigFixture` writes rigs no `.glb` ever existed for), which a blanket
  source-mandatory rule would make unbuildable. The refusal still lands exactly when a re-bake is
  needed but impossible. A read-only store answers fresh for what it carries (the `VatFreshness`
  precedent) and `pack` excludes `meshes_src` exactly as it excludes `textures_src`. *Rejected:
  load-and-warn — silently showing possibly-wrong data is the failure mode this feature ends; and
  source-mandatory-always, which cannot coexist with synthetic fixtures and adds no protection a
  token mismatch does not already trigger.*
- **ADR-6 — Freshness is decided per source group, and the group re-imports whole, bounded by
  what exists.** A source group is every container recording source S; one stale member
  re-imports them all in one pass, so the skeleton signature stays coherent — but the re-import
  writes only the containers that already exist, at the paths they already occupy: which outputs
  exist was an import-time dialog choice (`import_pipeline.cpp:105-112`), and a migration must
  not create a `.banim` the user declined or invent a path they did not choose. A rig may span
  two groups — the clips-only import writes a `.banim` from glb-B against a `.bskel` found by
  signature from glb-A (`import_writers.cpp:201-215`) — and coheres across a token change because
  that stales *every* group at once. The carried skeleton path (ADR-8) is the default; when the
  carried path no longer matches by signature, the re-import falls back to re-resolving the way
  the import did, and a fallback that finds nothing is what reports. The source-*hash* axis does
  not propagate across groups: a re-exported glb-A stales its own group alone, and clips from
  glb-B that no longer fit are caught by the existing signature check at load — loud, and no
  worse than today. Materials and
  textures are *not* re-imported: they are authored assets whose staleness `routeStamps` already
  governs, and re-importing them would clobber user edits. *Rejected: per-container freshness —
  a mesh at one token beside clips at another re-bakes half a rig, and an old `.banim` against a
  new `.bskel` is exactly the incoherence #413's debugging suffered.*
- **ADR-8 — Attached state survives the re-bake, keyed by name.** `toBMesh` yields every submesh
  unbound (`bmesh_io.h:73-83`: "this does not carry materials across, and nothing in assetlib
  does"); the bindings — `BMesh::materials`, each `Submesh::material`, and the skeleton
  references `BMesh::skeleton` / `AnimationSet::skeleton` — are attached state written after
  import (`attachMaterial`, `import_writers.cpp:107-118`) and again from the Material Editor,
  living *inside* the containers the re-bake replaces. The re-import carries them over from the
  old container before the new one is used or written, matching submeshes by the identity the
  container already stores — `Submesh::nameOffset` holds `<meshName>[primitive]`
  (`bmesh_gltf.cpp:1007-1011`), which survives the index shifts a skipped or added primitive
  causes. The name is free text and need not be unique — two glTF meshes named `Cube` yield two
  submeshes with one key — so a name that matches nothing *or* matches ambiguously is *reported* —
  a per-file failure in `migrate`, a load error in the editor — never guessed at. #418's rule that
  materials are authored only through the material editor makes this carry the *only* mechanism:
  a re-import can never re-author a binding it lost. *Rejected: doing nothing, which silently unbinds
  every submesh of every migrated mesh; carrying by index, which mis-binds silently when the set
  changes shape at equal count; and fuzzy matching across a changed set, which turns a visible
  report into an invisible mis-bind.*
- <a name="adr-7"></a>**ADR-7 — This reverses migrate-assets ADR-1** ("no migration system now"),
  whose trigger — concurrent developers — has fired as parallel agents. That plan is deleted in
  this PR, on explicit instruction; its ADR-4 (an unreadable `.bvat` re-bakes) outlives the
  record because [docs/vat.md](../vat.md) carries the behaviour, which is the document that
  matters. *Rejected: amending ADR-1 in place and keeping the file — the record's remaining
  content is superseded by this plan either way.*

## Non-goals

- File→file transform migrations, or any authored migration-script list.
- Migrating packed archives — the game path is untouched.
- Re-importing materials or textures (`routeStamps` staleness already governs them).
- `.bvat` and `.ktx2` — derived; the existing freshness machinery composes on top unchanged.
- Backfilling arbitrary old projects. `bernini-test-project` gets one final hand re-import as
  adoption — the last hand-carry. The repo's *own* committed fixtures are not exempt, though:
  `assets/Meshes/apples.bmesh` is loaded through writable loose stores (`examples/bgl_base`, the
  thumbnail test), so task 5 gives it a source and re-bakes it, or those loads fail.
- Editor re-import UX beyond what loading needs (no "Reimport" button; the import-conflict refusal
  stands).
- `.gltf` sources. Import refuses them with "export as .glb" (ADR-2); a multi-file source cannot
  be one copy and one stamp.

## Acceptance

Replay #413's failure *shape* on both surfaces, plus the refusals. The fixture is a small
purpose-built rig committed as a test asset — the dog itself is a licensed mesh and stays out of
this repo:

- A fixture whose `.banim` carries wrong values and a stale token beside its source `.glb`:
  an editor-side test proves the *loaded* clips carry the corrected data (in-memory re-bake, disk
  untouched); one `assetlib_cli migrate` run rewrites all three containers, and a second run
  rewrites nothing.
- The same fixture without its source: the load errors naming the missing source; `migrate`
  reports it failed and exits non-zero. Nothing is silently skipped.
- `pack` of a project with `meshes_src/` content excludes it.
- The re-baked mesh keeps its material bindings and its `.bskel` reference; a submesh set that
  changed shape is reported, never guessed at (ADR-8).
- `just test` green throughout; `Frozen_test` unchanged — the new fields default, so every
  pre-feature file stays readable at the `assetlib` layer.

## What the survey found

- **Import**: one glTF entry point, `loadFromGltf` (`libs/assetlib/include/assetlib/bmesh_gltf.h:28`)
  → in-memory `imp::BMeshImport`. The CLI writes every output through `bake`
  (`libs/assetlib/src/bmesh_io.cpp:344-375`); the editor calls the pieces itself
  (`apps/editor/src/Import/import_pipeline.cpp:161-227`, `import_writers.cpp`).
- **The source is unreachable after import.** It is read in place, never copied
  (`import_pipeline.cpp:63`); `BMesh` has no source field
  (`libs/assetlib_structs/include/assetlib_structs/BMesh.h:16-34`), `Skeleton` none,
  `AnimationSet` stores only the `.bskel` path (`Animation.h:44-46`).
- **`SourceStamp`** is `{size, content hash}` (`SourceStamp.h:15`) and exists on `.bmaterial`
  routes, `.bvat` and `.bsky`/`.benvl` only; staleness queries live in
  `libs/assetlib/src/mounted_io.h:87-100` and wrap onto `AssetStore`
  (`AssetStore_Staleness.cpp:19-47`).
- **Version constants** are one pair per container in each `*_io.cpp`, handed to
  `chunk::Writer::Finish` (`libs/assetlib/src/chunk_io.h:172`).
- **Editor import** is drag-drop onto the Content Explorer only
  (`ContentExplorerWindow.cpp:445-462`); import over existing names is refused, not repeated
  (`import_pipeline.cpp:104-146`); no re-import affordance exists anywhere.
- **Categories**: ten, in one list — `Project::c_RequiredDirectories`
  (`apps/editor/src/Project/Project.h:32-37`); `Create` scaffolds all, `Open` re-creates missing
  ones; an import dialog's folder field can only nest inside its category
  (`apps/editor/src/util/asset_paths.cpp:63-86`).
- **Pack** excludes `textures_src` by exact path component (`libs/assetlib/src/pak_pack.cpp:23-34`,
  `:119-121`); entry is decided by `assetTypeFromExtension` (`asset_refs.cpp:234-258`), which does
  not know `.glb` — so a source would be skipped incidentally today; the exclusion makes it
  deliberate.
- **Deletion protection** for `textures_src` is by reference edge (`RefKind::kChannelRoute`,
  `libs/assetlib/include/assetlib/asset_refs.h:29`), not by name — the same mechanism serves the
  source `.glb`.
- **Load chain**: `Project` → `AssetStore` (always loose in the editor) →
  `AssetManager::AcquireMesh` → `LoadMesh` → `deserialize`
  (`libs/gamelib/src/AssetManager.cpp:279-340`, `AssetStore_Containers.cpp:16-20`). Previews and
  thumbnails load by host path via `assetlib::load(absolutePath)` outside the store
  (`AnimationPreviewWindow.cpp:224`, `MaterialPreviewWindow.cpp:201`,
  `AssetThumbnailCache.cpp:146-147`) — they need the same seam or the dog would still look wrong
  exactly where it is debugged.
- **The load-time re-bake precedent**: `EnsureVatBaked` treats an unparseable `.bvat` as absent
  and re-bakes at load (`libs/gamelib/src/vat_freshness.cpp:50-92`), a read-only store answering
  fresh — but it writes to disk, which is correct only because `.bvat` is git-ignored derived
  output; hence ADR-4's in-memory rule for version-controlled containers.
- **`migrateProject`** walks the host filesystem, round-trips each container, byte-compares so an
  unchanged file is never dirtied, previews then writes, isolates errors per file
  (`libs/assetlib/src/migrate.cpp:60-103`); the CLI confirms between the walks
  (`libs/assetlib/cli/main.cpp:784-841`).
- **Doc drift to fix where touched**: `docs/asset_standards.md:815` lists five categories of the
  ten; its container versions at `:398-432` disagree with the `*_io.cpp` constants;
  `docs/archives.md` cites two wrong `pak_pack.h` anchors (one past the end of the file, one
  landing in a doc comment).

## What changes

- `assetlib_structs`: `BMesh`, `Skeleton`, `AnimationSet` gain a source path, a `SourceStamp` and
  an import-revision token (default 0), registered in the schema with defaults so old files read.
- `assetlib`: the token constant; writers stamp the three fields; an import-freshness query and an
  in-memory re-import seam over `AssetStore`; the `meshes_src` pack exclusion; a source reference
  edge in `asset_refs`; `migrateProject` re-imports stale sources, grouped per source.
- `gamelib`: `AssetManager`'s geometry acquires go through the current-or-rebaked seam;
  `VatFreshness` gains the token axis and `EnsureVatBaked` bakes from the seam's outputs (its
  disk write stays — `.bvat` is derived and git-ignored).
- `editor`: the `meshes_src` category; import copies the `.glb` and records it; previews load
  through the seam; a missing source surfaces like an unreadable container.
- `docs`: the re-bake story onto `asset_schema.md` (or its own page if it outgrows a section);
  the category list and version drift fixes; `vat.md` gains the token axis of VAT freshness and
  that a `.bvat` bakes from the seam's outputs.

What could break: **import determinism** — if re-import is not byte-stable, `migrate` never
converges; mitigated by the byte-compare and by rewriting only the three geometry containers.
**Load cost** — freshness hashes the source once per load, memoized against size+mtime as
`stampOf` already is; a re-import costs seconds and happens only when stale. **The refusal seam** —
the rule lives at the store/manager layer, never in `deserialize`, so `Frozen_test` keeps reading
its files at the `assetlib` layer; synthetic fixtures (`gamelib`'s `RigFixture`, the editor's test
writers) are current-by-construction under ADR-5 and keep loading; the committed `assets/`
binaries are token-0 stale and are re-baked with sources in task 5.

## Tasks

1. **The plan** — this file, and the deletion of `docs/plans/migrate-assets.md` (ADR-7).
   Gate: review.
2. **assetlib: geometry containers record their source** — the three fields on `BMesh`/`Skeleton`/
   `AnimationSet` and their schema registrations with defaults. The writer stamps the engine's
   token *constant*, never the struct's value — which is what makes a synthetic fixture current by
   construction (ADR-5).
   Gate: `just test assetlib` — round-trip tests show the fields survive; `Frozen_test` proves
   every pre-feature file still reads, defaulted.
3. **assetlib + editor: import copies the source** — `meshes_src` joins `c_RequiredDirectories`
   (in `assetlib` once #418 moves `Project` down), the copy + stamp in the single post-#418
   import path with the writers recording the fields, the `.gltf` refusal, the dialog folder
   field, the pack exclusion component, and the source reference edge so a referenced `.glb`
   cannot be deleted (whose header comment currently calls a `.glb` "waiting to be imported" —
   `asset_refs.h:48-49` — a line this feature falsifies).
   Gate: `just test editor` — an import round-trip shows the copy, the recorded reference and the
   edge; the `.gltf` refusal; an assetlib pack test shows `meshes_src` excluded.
4. **assetlib: freshness + in-memory re-import seam** — the token constant, the per-source-group
   staleness query (read-only store → fresh), and a re-import that returns a group's existing
   outputs in memory, attached state carried per ADR-8.
   Gate: `just test assetlib` — token mismatch and stamp mismatch each yield re-imported values
   with bindings and skeleton references intact; an unmatched submesh name reports; fresh leaves
   disk bytes untouched.
5. **gamelib + editor: loads go through the seam** — `AssetManager` geometry acquires and the
   preview/thumbnail host-path loads; a stale container whose source is missing errors like an
   unreadable file; `VatFreshness` gains the token axis so `EnsureVatBaked` re-bakes a `.bvat`
   whose geometry group is stale, from the seam's outputs (ADR-4). `assets/Meshes/apples.bmesh`
   (under `assets/Data/` once #418 makes `assets/` a project) gains its source and is re-baked,
   since the examples and the thumbnail test load it through writable stores.
   Gate: `just test gamelib editor` — the dog-shaped fixture loads corrected clips with the disk
   file unchanged; the source-less fixture errors; a pre-change `.bvat` over a stale group
   re-bakes; the existing suites still load `assets/`.
6. **assetlib: `migrate` re-imports on disk** — stale containers grouped by source, one re-import
   each, the existing outputs written through the current writers, byte-compared,
   preview-then-write; a missing source or an unmatched submesh is a per-file failure, never a
   skip. Geometry is intercepted *ahead of* the plain resave (`migrate.cpp:81-88`), which would
   otherwise stamp the current token into a stale source-less file and launder it into a
   "current" one.
   Gate: the CLI dog replay — one run rewrites the containers, a second rewrites nothing, the
   source-less case exits non-zero.
7. **docs, and the plan's deletion** — the pages above; this file goes with the feature's last PR.
   Gate: review.
