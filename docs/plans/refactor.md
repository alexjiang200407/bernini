# assetlib: one store, one codec per container

Builds [docs/specs/assetlib_store_codecs.md](docs/specs/assetlib_store_codecs.md). The spec holds
the design and the standard it follows; this plan holds the decomposition and what the survey found.

## Context

`AssetStore` owns a project's reads and none of its writes. A read is `store.LoadMesh(key)`; a write
is `save(mesh, store.ResolveWritePath(key))`, or `dataRoot / relative` joined by hand. #463 widened
the gap rather than closing it — six `LoadRegen*` methods joined the read half and no write half
arrived, so the store now answers **twenty loads and zero saves**. Both bake descs still re-carry a
data root the store holds, and `bake_tokens.h` made the container list a **seventh** place spelled
out by hand. That seventh site is what makes this urgent: a token exists to be bumped when a
writer's layout changes, so a container registered in six places and missed in the seventh is one
whose stale files are read as current.

## Decisions

- **ADR-1 — A codec is a compile-time `AssetCodec<T>` trait, not a runtime registry.** *Rejected:
  Godot's `ResourceFormatLoader`/`ResourceFormatSaver`, the actual standard, because our containers
  are unrelated PODs with no common base; it would return `std::any` and fight ROADMAP's DOD rule.
  A named deviation, recorded in the spec.*
- **ADR-2 — `AssetStore` owns both directions**: `Load<T>`/`Save<T>` replace twenty `Load*` methods
  and every `save*`. *Rejected: adding `SaveMesh`/`SaveMaterial`/… beside them, which fixes the
  asymmetry and leaves the enumeration.*
- **ADR-3 — The bake token moves into the trait**, beside the `Serialize` it has to move with.
  *Rejected: leaving `bake_tokens.h` as a table, which is the seventh site.*
- **ADR-4 — The `save*`/`load*`-by-`std::filesystem::path` family is deleted outright.** This
  **reverses** the spec, which kept it "for files no project owns"; the spec is amended in this PR,
  since an ADR is amended only by the change that reverses it. *That carve-out has zero users:
  all 76 production call sites address a file inside a project, the CLI's `out` paths included —
  they already go through `ResolveWritePath`. Keeping an escape hatch nobody uses is keeping the
  second way to do one thing that [CLAUDE.md](CLAUDE.md) § The bar each subsystem is held to
  forbids. Rejected: keeping them for tests, which is the same thing with a nicer excuse — tests get
  a `StoreAt` helper instead.* `writeObj`, `loadFromGltf`, `Project::Open` and `packProject` keep
  their `path` parameters: they address the host, which is a different question.
- **ADR-5 — `mounted_io.h` is deleted too, not kept under the templates.** *The survey found it is a
  third parallel family — one `load(fileSystem, key)` per container, which `AssetStore::Load*`
  forwards to. Rejected: making `Load<T>` forward to it, which would leave three families as two.*
- **ADR-6 — `LoadRegen*` stays a separate seam.** *Regeneration re-cooks from a copied source on a
  cache miss: a different operation with a different failure mode, asked for deliberately. Rejected:
  folding it into `Load<T>` behind a flag.*

## Non-goals

- **No on-disk format change.** Byte-identical containers, no token bumped, no re-bake. A bumped
  token in this feature's diff is a bug.
- **No new container type**, and no change to what any container stores.
- **Not a rewrite of the container layer.** `cache_io.h`, `json_doc.h` and the document seam are
  what the codecs call, unchanged.
- **No editor restructuring.** Its call sites change because their callee changed; nothing else.
- **Not runtime-registrable.** No plugin story, and none wanted.
- **No change to `Project`, `AssetRefGraph`, the prune, or the pack rules** beyond the call sites
  the deletions force.

## Acceptance

- Every container round-trips through the store — `Save<T>` then `Load<T>` by one key — and the
  bytes match what the pre-refactor writer produced, so "no format change" is asserted, not claimed.
- `TokenCanary_test` passes **unchanged**. Moving a token must not move its value.
- A key containing a `\` is refused rather than silently resolving loose. Nothing pins this today,
  and it is the failure the seam exists to prevent.
- `grep -r "save[A-Z]\w*(.*std::filesystem::path" libs/assetlib/include` returns nothing.
- `just test` green, and `assetlib_cli describe|migrate|pack` behave identically on the test project.

## What the survey found

**Three parallel families, not two.** Alongside the public path-taking pair and
`AssetStore::Load*`, [libs/assetlib/src/mounted_io.h](libs/assetlib/src/mounted_io.h) is an internal
seam with one `load(fileSystem, key)` per container; the store's methods are thin forwarders to it
(`AssetStore_Containers.cpp` is 12 one-line bodies). Its own comment states the rule ADR-4
overturns: *"The path-taking overloads stay public: they address a file on the host that no project
owns."*

**Counted call sites**, precisely:

| | Count | Where |
|---|---|---|
| Free `save*`/`load*` by path | 281 | 76 production, ~205 test |
| `AssetStore::Load*` | ~65 | mostly `LoadRegenMesh` (16), `LoadMesh` (8), `LoadMaterial` (8) |
| `MaterialBakeDesc` / `EnvBakeDesc` | 62 | assetlib, editor, CLI |
| Container list spelled out | 7 | `container_format.h`, `assetTypeFromExtension`, CLI `sniff`, `migrate.cpp`, `asset_rename.cpp`, `pak_pack.cpp`, `bake_tokens.h` |

**Tests already have a mount helper but not a store one.** `MountAt(dir)` returns a
`LooseFileSystem` and is used by 18 of 51 assetlib test files; the single-argument
`AssetStore(dataRoot)` constructor is what a `StoreAt(dir)` would wrap.

**Both container families kept one seam.** #463's documents (`.bmaterial`, `.benv`, `.bimport`) and
cache entries (`.bmesh`, `.bskel`, `.banim`, `.bvat`, `.bsky`, `.benvl`) both expose
`serialize`/`deserialize` over bytes, so one trait serves both. Only the cache entries have a token.

## What changes

Additions are all task 1; every removal is a later task, so the surface is reviewable before
anything moves.

| Area | Change | Task |
|---|---|---|
| `include/assetlib/AssetCodec.h` | **New**: the primary template, the concept constraining it, the registration list and the table | 1 |
| `include/assetlib/*_io.h` | **Gains** its `AssetCodec<T>` specialization | 1 |
| `include/assetlib/AssetStore.h` | **Gains** `Load<T>`/`Save<T>` and the bake methods | 1 |
| `include/assetlib/AssetStore.h` | Twenty `Load*` become forwarders; six `LoadRegen*` untouched | 2 |
| `src/mounted_io.h`, `src/AssetStore_Containers.cpp` | Deleted | 2 |
| `src/bake_tokens.h` | Deleted; values move into the specializations | 3 |
| `material_bake.h`, `env_bake.h` | Descs deleted | 4 |
| `asset_refs.cpp`, `migrate.cpp`, `asset_rename.cpp`, `pak_pack.cpp`, CLI `sniff` | Read the table | 5 |
| `apps/editor`, `libs/gamelib`, CLI | 76 production call sites | 6 |
| Four test suites, `include/assetlib/*_io.h` | ~205 test call sites; then `save*`/`load*` by path deleted | 7 |

**What could break.** The riskiest step is task 5: `migrate` and `pack` re-derive what a container
*is* from the table, and a container missing from the registration list would silently drop out of
both — a project that packs without its skies. The registration list gets a static assertion that
it covers every `AssetType`, and `Pack_test` already pins what an archive holds.

## Tasks

**The whole new public interface lands first, in one task, adding only.** Task 1 is the complete
API — every codec specialization, both store templates, the bake methods and the table — with not
one existing declaration removed. Everything after it is migration and deletion against a surface
that is already fixed.

*Rejected: letting the API arrive across four tasks as each area was migrated (trait in 1, token in
3, bake methods in 4, table in 5), which was this plan's first shape. It spreads the design over
four reviews and never shows the finished surface until the last one has landed — by which point
disagreeing with it is expensive. The cost of doing it this way is a window, tasks 1–7, where both
APIs exist side by side; that is the deliberate price of reviewing the new one whole.*

1. **`feat(assetlib): the public API — a codec per container, a store that writes`** — additive,
   deleting nothing. `AssetCodec.h` (primary template, the concept constraining it, the
   registration list), a specialization per container, `AssetStore::Load<T>`/`Save<T>`, the store's
   bake methods, and the table with its totality assertion. The old `save*`, `mounted_io.h`,
   `bake_tokens.h`, the twenty `Load*` and both bake descs all still stand and still work.
   Gate: a new `Codec_test` drives **the new API alone** — round-trips every container through the
   store and byte-compares each against the existing writer's output, so the two surfaces are
   proven to agree before either moves.

Then the migration, each step behind the surface task 1 fixed:

2. **`refactor(assetlib): the store's own reads go through the codec`** — `mounted_io.h`'s container
   half and `AssetStore_Containers.cpp` deleted; the twenty `Load*` become forwarders to `Load<T>`.
   Gate: `just test assetlib`.
3. **`refactor(assetlib): the bake token lives with its writer`** — `bake_tokens.h` deleted, values
   into the specializations. Gate: `TokenCanary_test` **unchanged** and passing.
4. **`refactor(assetlib): a bake reads and writes through the store`** — `MaterialBakeDesc` and
   `EnvBakeDesc` deleted, their call sites onto the store's bake methods. Gate:
   `just test assetlib editor`.
5. **`refactor(assetlib): one table answers what a container is`** — the seven sites read the table.
   Gate: `just test assetlib`, and `Pack_test` proves nothing dropped out of an archive.
6. **`refactor(assetlib,gamelib,editor): every production write goes through the store`** — the 76
   production call sites converted. Gate: `just test`.
7. **`refactor(assetlib): the path-taking family goes`** — `StoreAt` for tests, ~205 test call sites
   converted, then the `save*`/`load*`-by-`path` declarations deleted. Last, because it is what
   makes the old surface unreachable. Gate: `just test`, plus the `grep` in Acceptance returning
   nothing.
8. **`docs(assetlib): the API map after the seam closed`** — rewrite
   [docs/assetlib_api.md](docs/assetlib_api.md)'s half-built-seam section, delete the spec and this
   plan. Gate: every link resolves.

Tasks 1–5 are assetlib-only and land bottom-up. Task 6 is where the other subsystems move, and
task 7 is deliberately last: every call site converts to a seam that stopped changing at task 1.
