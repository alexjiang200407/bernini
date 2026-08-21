# migration-system-v2 — authored data is text, derived data is a keyed cache

## Context

Parallel AI agents each change asset code in their own worktree, and the shared test project's
binaries can only hold one branch's state — debugging a second branch means merging binaries git
cannot merge. #413 (the dog) showed the deeper limit: the schema system (#396) converts any
*layout*, but the `.banim` values were wrong with the layout identical, and only the source `.glb`
held the truth — which nothing recorded and nothing could reach. A week earlier the schema landing
itself needed a hand re-bake. Every recent capability question ("can it fix the dog?", "can it
handle a mesh field that depends on a material?") had the same answer: outside its domain.

The root cause is one design fact: **authored state and derived state are tangled in the same
binary files.** Submesh→material bindings live inside `.bmesh`; the material's graph — already a
JSON string (`BMaterial::editorGraph`) — lives inside a binary chunked container. Derived data
could always be remade from its inputs; authored data can never be, and burying it in derived
binaries is what forces salvage machinery, unmergeable LFS conflicts, and migration itself.

This feature untangles them, and that dissolves migration as a concept:

- **Authored data becomes small text documents** — diffable, and two branches merge them like code.
- **Derived data becomes a keyed cache entry** — a pure function of stamped inputs, regenerated on
  any mismatch, never migrated and never parsed in an old shape.
- **The schema system is deleted** — old payloads are never read, so their structure can be
  forgotten; the by-name-with-defaults tolerance it provided lives on in the text formats for the
  only files that still need it.

**Dependency:** this plan assumes `feat/remove-non-project-assetlib` (plan merged as #418) lands
first: `Project` down in `assetlib`, one project-based `--project` import path, `assetlib::bake`
deleted, `assets/` itself a project. One import path is one place to stamp cache keys. The survey
below cites the pre-#418 tree; the tasks name where code lives after it.

## Decisions

- **ADR-1 — The asset model splits authored from derived, by file.** Authored: the material
  document, a per-source import document, an environment document, source files (`.glb`, source
  textures, `.hdr`). Derived: `.bmesh`, `.bskel`, `.banim`, `.bvat`, the baked env maps, baked
  textures — binary, regenerable, never carrying authored state. The split cuts *through* the env
  family, which today mixes both: `.benv` is pure authored composition (`{name, sky, lighting}`,
  `BEnv.h:51-56`) and becomes an authored document outright; the authored values living in the
  derived containers — `BSky::mipLevel` and `rotationY` ("a change a person makes and looks at
  immediately"), `BEnvLighting::exposureOverride` (kept across re-bakes, `benvl_io.cpp:36`) —
  move into that document, leaving `.bsky`/`.benvl` purely derived and those fields gone from
  their structs. This is a deliberate semantics change: the values become **per-environment**
  where they were per-sky/per-lighting — two environments sharing one sky (`BEnv.h:47-49` names
  that sharing as the point of composition-by-path) get independent rotations and overrides,
  which is right because these are "what the person looking at *this* environment chose". The
  consumers re-point at the document's values, carried on the *resolved* environment:
  `AssetManager.cpp:187-190` and `:198` (which reads `lighting.exposure` and must become the
  effective value — today it ignores an authored override), `env_resolve.cpp:35-36` and `:44`,
  and the two *writers*: `assetlib_cli exposure` (`cli/main.cpp:1060-1069`), which today edits a
  `.benvl` and must re-point at the environment document — its argument becomes the `.benv`,
  since per-environment values leave "set it on this `.benvl`" with no unique destination — and
  the env import's `--skybox-mip` (`env_import.cpp:196`), which today writes `mipLevel` clamped
  against the mip chain the bake just produced. The document stores the *requested* value —
  an authored value must not be computed from a bake — and the resolve clamps at read against
  the sky's actual mip count. One deliberate edge case:
  the textures the import extracts from a `.glb` into `textures_src/` are *promoted to authored
  sources at extraction* — a snapshot the user may edit and the routes stamp; regeneration never
  rewrites them, so an edit to the `.glb`'s embedded textures does not propagate, exactly as
  materials do not. *Rejected: keeping bindings inside `.bmesh` and salvaging them across
  re-bakes (the previous plan's ADR-8) — permanent carry machinery for a self-inflicted tangle;
  and converting the env containers wholesale as derived — a token bump would silently reset
  `rotationY` and drop `exposureOverride`, the authored-loss failure this ADR exists to prevent.*
- **ADR-2 — A derived container is a cache entry: a frozen header over raw current chunks.** The
  frozen part — magic, header version, the cache key (source content hashes, the bake-revision
  token, authored-input hashes), and the **chunk table** (id → offset/size) — is the one layout
  kept forever; the chunk table stays because the ranged reads the roadmap ticks as done
  (`loadMeshRefs`, the staleness surveys — "a few hundred bytes per container, not the whole
  project") need it. The chunks themselves are raw current-layout structs with no
  self-description: a matching key means the current code wrote them, so the fixed typed reader
  reads them; any mismatch — stale, foreign branch, newer, truncated, corrupt — is a cache miss
  and regenerates (today's `.bvat` rule, promoted to the design). Chunk 0's schema is gone.
  *Rejected: schema chunks and by-name conversion — machinery to parse old payloads that are
  never parsed again; access-time dynamic layout (uniforms-style) — it re-solves the solved axis
  and fights DOD on hot paths; header without a chunk table — turns every refs survey into a
  whole-file read.*
- **ADR-3 — Derived geometry stays committed (LFS); regeneration at load is in-memory; writing is
  explicit.** Committed keeps clones and CI warm. Merge conflicts on derived files become trivial:
  either side is a valid resolution, because a wrong pick is a stale key that regenerates on next
  load. Write-on-load would dirty every checkout on every branch switch, so the editor regenerates
  in memory and `assetlib_cli migrate` (or a deliberate save) writes current bytes back. `.bvat`
  stays git-ignored and keeps writing at load (pure local cache). `.ktx2` is a foreign format that
  cannot carry our header; it keeps its current stamp-governed regime under the material's routes.
  *Rejected: binaries out of git entirely — cold clones and CI bake for nothing at this team size;
  write-on-load — permanently dirty trees.*
- **ADR-4 — One bake-revision token per container kind, compared for inequality.** A `constexpr
  uint64_t` changed to a fresh random value on **any** output-affecting change — semantics (the
  dog) *and* layout, since the chunks are schema-less and a forgotten bump would parse garbage.
  Per-import parameters are *not* covered by the token — `sampleRate` is user-settable today
  (`assetlib_cli`'s `-r,--sample-rate`, `cli/main.cpp:193-194`), so a token bump would silently
  regenerate a 60 Hz `.banim` at the default rate, #413's exact shape. Parameters live in the
  import document (ADR-6), whose parameter half hashes into the key (bindings stay out — ADR-6)
  and is what regeneration reads them from.
  *Rejected: separate layout/semantic knobs — two constants, one purpose, double the
  forgetting; a monotonic counter — two branches bumping to the same number are indistinguishable,
  the trap that disqualified version numbers.*
- **ADR-5 — Materials become authored text documents.** `BMaterial::editorGraph` is already a JSON
  string; the document holds it plus the factors and routes, and the binary wrapper goes. The
  editor stays the sole author of material *content* (#418's rule). Readers take the keys they
  know, default the rest, and **preserve unknown keys on round-trip** — dropping them would
  silently destroy a sibling branch's work on merge. Baked texture outputs stay governed by the
  routes' `SourceStamp`s. The one-time carry is `loadMaterial` → write the document — a pure
  struct→text re-serialize, no QtNodes board involved, so it is lossless and may run in the CLI
  without violating editor-only authoring. (Known hazard nearby, pre-existing and out of scope:
  the Material Editor's graph-less seeding drops texture routes — the `qWarning` at
  `MaterialEditorWindow.cpp:842-847` claims routes are rebuilt, but the seeding at `:852-869`
  fills factors only; the carry must not route through that path.)
  *Rejected: keeping `.bmaterial` binary under the schema — the most-churned container in the
  tree (major 11) stays unmergeable forever; carrying via the editor board — lossy today.*
- **ADR-6 — Each source gets one authored import document; parameters key the bake, bindings are
  applied at load.** Per `.glb`: the import parameters (`sampleRate`) and the submesh-name →
  material bindings of its outputs (what `attachMaterial` records today), written by the import
  and the attach flows. The *initial* import is the only time the source's material table authors
  anything — it seeds the material documents and this document's bindings; a regeneration authors
  nothing, so a re-exported source that dropped or reassigned a material changes no material
  document and no binding: the authored material survives, held by its reference edges until the
  user unbinds and deletes it, and its binding stands as long as the submesh it names exists.
  The two halves feed different stages, deliberately. *Parameters* change
  what the importer computes, so they hash into the cache key and a change regenerates.
  *Bindings* touch no imported byte — so they stay **out** of the key, and a writable-store load
  fills `BMesh::materials` / `Submesh::material` from the document over whatever was loaded or
  regenerated: a rebind is a document edit that costs the next load nothing, needs no source, and
  can never make a mesh stale (`MaterialEditorWindow.cpp:776-777`'s attach-then-save becomes a
  document write). A mesh with **no** document keeps its loaded fields untouched — which is what
  keeps synthetic fixtures (`RigFixture` pushes its bindings straight into the struct) working,
  the same current-by-construction rule ADR-7 gives them for the key. The application *rebuilds*
  `materials` canonically from the document — never mutates the loaded array via `attachMaterial`,
  whose slot reuse makes the result depend on the file's history and would leave two checkouts
  with identical documents holding different bytes forever. The struct fields remain — applied at
  load in the editor, baked in by every writer (the import's bake, a deliberate save, `migrate`,
  `pack`) — so the draw path (`AssetManager.cpp:313-328`) and the packed game see a bound mesh
  exactly as today, and a read-only store uses the baked-in bindings as-is. Because a rebind does
  not stale the key, `migrate` and `pack` read every mesh **through the seam** — document applied
  regardless of freshness — or an archive packed after a rebind with no `migrate` run would ship
  the old binding silently. The key is
  the submesh name; the import *refuses* a source whose submesh keys collide (unnamed or
  duplicate mesh names — "name the meshes in the DCC"), because a colliding key can only mis-bind
  silently. A binding naming a submesh the mesh no longer has is *reported* (editor warning,
  `migrate` failure, and `pack` fails the pack, consistent with ADR-8's un-re-bakeable rule) —
  no carry, no guessing. The `RefKind::kSubmeshMaterial` edge's referrer
  becomes the import document. The parameter half generalises: "derived depends on X" is always
  "X is a stamped input", the relationship mechanism the schema never had. *Rejected: bindings in
  the material (many meshes share one), in the level (per-instance override already exists),
  index-keyed (silent mis-bind when the submesh set shifts), collision-tolerated (ditto), and
  bindings hashed into the key — a rebind would stale the group, re-import the `.glb` on every
  load until `migrate` runs, and refuse outright on a project adopted without sources.*
- **ADR-7 — The unit of regeneration is the source group, bounded by what exists.** All geometry
  outputs of one `.glb` regenerate together (skeleton signature coherence); only containers that
  already exist are written, at their existing paths; a clips-only group re-resolves its skeleton
  by signature and reports a vanished match. The bound cuts both ways: an existing container the
  re-exported source no longer produces (a `.glb` whose skeleton was deleted, with a `.bskel` on
  disk) is *reported* — its key is stale and its regeneration is impossible, so the editor load
  errors, `migrate` fails the file, `pack` fails the pack — never silently deleted and never
  served stale; deleting it (and its dependents) is the user's act through the normal
  reference-edge cascade, the alternative being restoring the skeleton in the DCC. An entry whose source is missing or was never
  recorded refuses to load when stale, surfaced like today's unreadable-container errors; a
  current-keyed entry loads without its source, which keeps synthetic test fixtures
  (`RigFixture`) buildable — writers stamp the engine's constants, never a stored value, so they
  are current by construction. Import copies the self-contained source into the project
  (`meshes_src/`, the `textures_src` pattern); `.gltf` is refused with "export as .glb".
  *Rejected: per-container freshness — half a rig re-bakes; blanket source-mandatory —
  unimplementable for synthetic fixtures; sidecar-set copying for `.gltf` — a directory contract
  nothing can stamp.*
- **ADR-8 — A read-only store trusts its keys because `pack` makes them true.** The seam skips
  regeneration on a read-only store (the game cannot bake), which is only sound if nothing stale
  ever enters an archive: with schema-less chunks, a stale entry read as current parses garbage —
  and a size-compatible layout change parses it *silently*. So `pack` extends its existing
  stale-`.bvat` re-bake to every derived entry: a stale geometry group is re-baked into the
  archive, and a group it cannot re-bake (missing source) fails the pack. The roadmap already
  states the discipline for `.bvat`: "a shipped archive is correct by construction". "Every
  derived entry" is implemented in two steps: geometry when the writing paths land (task 6), the
  `.bvat`/env cache entries with their conversion (task 7, whose gate carries the archive
  property; `.bvat`'s stale-in-pack re-bake exists today and re-keys with it). Authored documents
  (the material and env documents) ship in the archive verbatim and are parsed at load — the
  runtime already reads both kinds back (`AssetManager.cpp:177,230`), the text parse replaces the
  chunk parse, and the game skips the `editorGraph` it never needed. The import document is the
  exception: it does not ship — a read-only store uses the baked-in bindings, so `pack` excludes
  it deliberately rather than by the extension filter's default. *Rejected:
  packing bytes verbatim and hoping the project was current — the one silent-corruption path the
  model would otherwise contain.*
- **ADR-9 — The schema system is deleted, last, and the frozen fixtures go with the task that
  strands them.** Schema riders beyond geometry — `.bvat`, `.bsky`, `.benvl`, `.benv` — convert
  in task 7 (they already store input paths + stamps, so they are nearly cache entries today; the
  chunk-0 wrapper is replaced by the header, and `.benv` becomes the authored document). `assets/Frozen/` and `Frozen_test` exist
  to prove old *payloads* stay readable — a promise this model deliberately ends — so each
  Frozen case is deleted in the task that converts its container (geometry cases with the
  geometry task, the material case with the material task, env with env), keeping `just test`
  green at every task boundary. The final task deletes `libs/schema`, `AssetSchemaBuilder`, the
  hooks, `describe --schema`, and reworks `inspectContainer` to print the header and key.
  *Rejected: deleting schema first — it strands every container and the material carry; keeping
  it "just in case" — the unused-subsystem accumulation this repo has removed twice before; one
  big red window for `Frozen_test` — a task that leaves the tree failing defeats bisection.*
- **ADR-10 — VAT freshness gains the token axis.** `VatFreshness` compares `.bvat` stamps against
  disk files, so with geometry regenerated in memory it would call a pre-change `.bvat` fresh and
  the VAT tier would render old geometry beside a fresh skinned tier. A `.bvat` whose geometry
  group is stale is itself stale, and `EnsureVatBaked` bakes from the seam's outputs (it already
  reads inputs through the store) and keeps writing disk. *Rejected: stamp-only VAT freshness —
  the two tiers split.*
- **ADR-11 — This reverses migrate-assets ADR-1** ("no migration system now"): its trigger —
  concurrent developers — fired as parallel agents. That plan is deleted in this PR, on explicit
  instruction; its ADR-4 (`.bvat` re-bakes) outlives it in [docs/vat.md](../vat.md).

## Non-goals

- Any migration mechanism for derived data: no file→file transforms, no authored migration lists,
  no history replay. Stale is regenerated, never converted.
- Cache warming or sharing for huge projects (CI artifacts, shared cache dirs). Cold regeneration
  per checkout is accepted *for now*; when a texture-heavy project makes bake time bite, that is
  a spec of its own, not silent scope creep here.
- `feat/remove-non-project-assetlib`'s scope (Project move, import-path collapse, CLI
  `--project`, `assets/` as a project) — it lands there; this feature builds on it.
- Re-extracting or re-encoding textures on regeneration (ADR-1's promotion rule; `.ktx2` stays).
- `.gltf` sources (ADR-7's refusal).
- Editor re-import UX beyond loading (no "Reimport" button; the import-conflict refusal stands).
- Backfilling arbitrary old projects. `bernini-test-project` gets one adoption pass (sources
  attached, materials carried) — the last hand-carry.

## Acceptance

- **Replay #413's failure shape** on a purpose-built rig fixture (the dog is licensed and stays
  out of the repo): a fixture whose `.banim` carries wrong values and a stale key beside its
  source loads *corrected* in the editor with the disk file unchanged; one `assetlib_cli migrate`
  run rewrites it; a second run rewrites nothing.
- The same fixture without its source: the load errors naming the source; `migrate` reports it
  and exits non-zero. Nothing silently skipped.
- **The merge property**: a derived file replaced by a stale-keyed sibling (either side of a
  conflict) regenerates on load — proven by a test that swaps in a wrong-keyed entry.
- **The archive property**: `pack` over a project with a stale geometry group re-bakes it into
  the archive; with the group's source missing, the pack fails. `meshes_src/` is excluded.
- **Materials as text**: an editor round-trip (author → save → load → identical), unknown keys
  preserved, and the test project's materials carried once, losslessly (routes intact).
- **Bindings applied, never baked-in-and-lost**: a re-bake keeps the bindings the document names;
  a binding-only edit rebinds on the next load with no regeneration — including on a group whose
  source was never recorded — and reaches an archive packed with no `migrate` run; a binding
  whose submesh vanished is reported; an import whose submesh keys collide is refused; a mesh
  with no document keeps its stored bindings.
- Ranged refs surveys (`loadMeshRefs`) still read a few hundred bytes, not the file.
- `just test` is green after every task, and stays green in the final task with `libs/schema`
  gone.

## What the survey found

(Established across this feature's grill and prechecks; file:line verified 2026-08-20, pre-#418.)

- One glTF entry point, `loadFromGltf` (`bmesh_gltf.h:28`); the CLI writes via `bake`
  (`bmesh_io.cpp:344-375`), the editor via its own writers (`import_pipeline.cpp:161-227`,
  `import_writers.cpp`) — the duplication #418 removes.
- The source is read in place and never copied; no container records it (`BMesh.h:16-34`,
  `Animation.h:44-46`). `SourceStamp` is `{size, content hash}` (`SourceStamp.h:16`), memoized
  against size+mtime (`bmaterial_io.cpp:288-309`), carried by `.bmaterial` routes, `.bvat`, and
  the env containers only.
- Bindings are attached state inside `.bmesh`: `toBMesh` yields every submesh unbound
  (`bmesh_io.h:73-83`), `attachMaterial` writes them at import (`import_writers.cpp:55-126`) and
  from the Material Editor; the draw path consumes `BMesh::materials`/`Submesh::material`
  (`AssetManager.cpp:313-328`). `Submesh::nameOffset` is `<meshName>[primitive]`, suffixed only
  for multi-primitive meshes (`bmesh_gltf.cpp:1008-1011`); the name is free text, may be empty,
  and need not be unique — hence ADR-6's collision refusal.
- `BMaterial` is small and mostly text already: `editorGraph` (a serialized JSON graph string),
  three texture paths, ~8 factors, 9 routes + stamps (`BMaterial.h:79-113`).
- Schema riders beyond geometry: `bvat_io.cpp:196,261`, `bsky_io.cpp:74,80`,
  `benvl_io.cpp:80,87`, `benv_io.cpp:65,74`, `env_route_io`, `container_info.cpp`,
  `AssetSchemaBuilder.h` — all construct or consume a `schema::Schema` (hence ADR-9's conversion
  task). `.bvat` and the env containers already store their input paths and stamps
  (`BVat.h:84-91`, `BEnv.h:15-23`).
- Ranged reads ride the chunk table: `loadMeshRefs` reads only `c_WantedRefChunks` through
  `readChunksFromFile` → `IFileSystem::ReadRange` (`bmesh_io.cpp:189-212`) — hence ADR-2 keeping
  the table.
- Ten project categories in `Project::c_RequiredDirectories` (`Project.h:32-37`; #418 moves the
  type down); `pack` excludes `textures_src` by exact path component (`pak_pack.cpp:23-34`,
  `:119-121`) and re-bakes stale `.bvat`s (`:112`) but packs everything else verbatim
  (`:117-131`) — hence ADR-8. Deletion protection is by reference edge (`asset_refs.h:25-29`).
- Load chain: `AssetStore` (always loose in the editor) → `AssetManager` acquires
  (`AssetManager.cpp:279-340`); previews/thumbnails load by host path outside the store and need
  the same seam. The load-time re-bake precedent, `EnsureVatBaked`, writes disk
  (`vat_freshness.cpp:50-92`) — legitimate only because `.bvat` is git-ignored, which ADR-3
  preserves and refuses to extend to committed containers.
- `migrateProject` walks, round-trips, byte-compares, previews-then-writes, isolates errors per
  file (`migrate.cpp:60-103`) — the walk and write discipline the new `migrate` keeps while its
  per-file action becomes "regenerate on key mismatch".
- The import extracts embedded textures into `textures_src/` (`bmesh_io.h:144-145`,
  `import_pipeline.cpp:130`) and routes materials at the extracted files — ADR-1's promotion
  rule.
- Doc drift to fix where touched: `asset_standards.md:815` lists five categories of ten; its
  container versions at `:398-432` disagree with the `*_io.cpp` constants; `archives.md` cites
  two wrong `pak_pack.h` anchors.

## What changes

- `assetlib_structs`: `BMesh::materials`/`Submesh::material` remain, but as applied output filled
  from the import document, no longer authored state; `BSky` and `BEnvLighting` lose their
  authored fields to the env document (ADR-1); `BEnv` becomes the document's in-memory form.
- `assetlib`: the cache header read/write (key + chunk table); the token constants; the key check
  + in-memory regeneration seam over `AssetStore`; the import and material document formats;
  `meshes_src` pack exclusion and reference edges; `pack` re-bakes stale groups; `migrate`
  rewrites them; `.bvat`/env converted to the header; `libs/schema` deleted at the end.
- `gamelib`: geometry acquires go through the seam; `VatFreshness` token axis; `EnsureVatBaked`
  bakes from seam outputs.
- `editor`: import copies the source and writes the import document; material save/load speaks
  the text format; previews load through the seam; missing source surfaces like an unreadable
  container.
- `docs`: `asset_schema.md` replaced by the cache-model page; `asset_standards.md`,
  `archives.md`, `vat.md`, `envmaps.md` updated where touched.

What could break: **regeneration determinism** — `migrate` byte-compares, so a nondeterministic
bake never converges; **a forgotten token bump on a layout change** — the chunks are schema-less,
so the rule is "any output-affecting change bumps", and ADR-8 closes the one *silent* path (a
stale entry inside an archive); **the material carry** — one-time, via struct→text with the old
reader still present until the final task, which is why deletion is last.

## Tasks

(Tasks 2–8 target the tree as `feat/remove-non-project-assetlib` leaves it.)

1. **The re-cut plan** — this file; deletes `docs/plans/migrate-assets.md` (ADR-11).
   Gate: review.
2. **assetlib + editor: sources copied and the import document authored** — import copies the
   `.glb` into `meshes_src/` (`.gltf` refused, submesh-key collisions refused), writes the import
   document (bindings + parameters), reference edges protect both; `pack` excludes `meshes_src`;
   the bake fills the mesh's material fields from the document. The attach flow writes the
   document *alongside* today's `attachMaterial` + mesh save — the dual write keeps rebinding
   working until task 3's load-apply supersedes the mesh-write half. Gate: import round-trip
   shows copy, document, edges, bound mesh; collision refusal; pack exclusion test.
3. **assetlib: the cache header and the seam on geometry** — header (key + chunk table) on
   `.bmesh`/`.bskel`/`.banim`, token constants, per-source-group key check (read-only store →
   fresh), in-memory regeneration returning a group's existing outputs with parameters read from
   the document and bindings applied over the result (ADR-6); missing source on a stale entry
   refuses. Deletes the geometry `Frozen_test` cases and their fixtures (ADR-9), and adopts the
   committed `assets/` geometry: `apples.bmesh` re-saves at the new header with its source
   (`assets/apples.glb`), and its import document is authored from the bindings already in the
   file — the smallest instance of the adoption pass, needed because `GltfSkin_test` and the
   thumbnail test assert those bindings.
   Gate: stale key and stale stamp each regenerate; a wrong-keyed entry swapped in regenerates
   (the merge property); a binding-only document edit rebinds the loaded mesh without
   regeneration, on a sourceless group too; fresh leaves bytes untouched; ranged `loadMeshRefs`
   still reads ranges; source-less stale refuses; `just test` green.
4. **gamelib + editor: loads go through the seam** — acquires and preview/thumbnail loads;
   `VatFreshness` token axis (ADR-10). Gate: the dog-shaped fixture loads corrected clips, disk
   unchanged; pre-change `.bvat` over a stale group re-bakes; suites still load `assets/`.
5. **Materials become text** (ADR-5) — the document format, editor save/load, unknown-key
   preservation, the struct→text carry of committed fixtures and the test project; route stamps
   unchanged. Deletes the `.bmaterial` `Frozen_test` cases. Gate: editor round-trip; carry
   losslessness (routes intact); `just test` green.
6. **assetlib: the writing paths** — `migrate` regenerates stale groups on disk (walk,
   byte-compare, preview-then-write; missing source or vanished binding is a per-file failure);
   both `migrate` and `pack` read geometry through the seam so a binding-only edit reaches disk
   and archive without a regeneration (ADR-6); `pack` re-bakes stale geometry groups into the
   archive and fails on an un-re-bakeable one (ADR-8). Gate: the CLI replay — one run rewrites,
   a second rewrites nothing, source-less exits non-zero; the archive property; rebind then
   `pack` with no `migrate` run — the archive carries the new binding.
7. **The derived remainder converts, and the env family splits** (ADR-1, ADR-9) — `.bvat` moves
   from chunk-0 schema to the cache header (its stored input paths + stamps become the key, its
   existing stale-in-pack re-bake re-keys with it); `.benv` becomes an authored document carrying
   the composition, `exposureOverride`, `mipLevel` and `rotationY`; the consumers re-point at
   the resolved environment (`AssetManager.cpp:187-190`, and `:198` becomes the effective
   exposure; `env_resolve.cpp:35-36`, `:44`, clamping the requested mip at read),
   `assetlib_cli exposure` takes the `.benv` instead of the `.benvl`, and the env import writes
   the requested `--skybox-mip` into the document (ADR-1); `.bsky`/`.benvl` become purely derived cache entries under
   the header;
   `pack`'s stale re-bake extends to them. The committed `assets/` forest set re-saves at a
   current key — its route sources were *never recorded* (empty `source` strings, no `.hdr` in
   the tree), so its key carries token + authored-input components only, and ADR-7's
   current-without-source rule keeps it loading. Deletes the env `Frozen_test` cases.
   Gate: `just test` — round-trips per container; a stale env entry regenerates; the archive
   property for env; a defaulted `rotationY`/`exposureOverride` cannot slip through — a test
   asserts the resolved environment carries the document's values; the golden suite still loads
   the forest set.
8. **Delete the schema system** (ADR-9) — `libs/schema`, `AssetSchemaBuilder`, chunk-0 machinery,
   hooks, `describe --schema`, the emptied `assets/Frozen/` and `Frozen_test`; `inspectContainer`
   prints the header and key; docs replaced, including the schema contract stated in root
   `CLAUDE.md`'s Documentation Index and `libs/assetlib/CLAUDE.md`; this plan deleted with the
   feature's landing. Gate: `just test` green with `libs/schema` gone.
