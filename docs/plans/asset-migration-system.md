# asset-migration-system — implementation plan

## Context

Every container refuses a major it does not recognise, and `.bmaterial` has bumped its major nine
times in six weeks (`git log -p libs/assetlib/src/bmaterial_io.cpp`) — a field added, a field
removed, once a field's meaning changed. Each bump makes every file on every branch unreadable until
re-baked. With two developers the number stops working altogether: branch A bumps `.bmaterial` to
11 adding one field, branch B bumps it to 11 adding another, and after the merge two incompatible
"v11" shapes exist that no version guard can tell apart.

None of the triggers [the spec](../specs/asset_container_migration.md) named has fired. The change
is made now anyway, because its one hard bump — files written before it carry no schema and cannot
be read by what replaces them — is cheapest while there is exactly one project to re-bake.

## Decisions

- **ADR-1 — Build it now, reversing [migrate-assets](./migrate-assets.md) ADR-1 and the spec's
  deferral knowingly.** *Rejected: waiting for a trigger; the transition cost only grows with the
  number of files and people.*
- **ADR-2 — Self-describing files.** Every container carries a schema table naming each struct it
  stores and that struct's fields (name, kind, offset, count); one generic reader diffs the file's
  schema against the current one **by name** and copies field by field. Layout comes from the file,
  never from the version number, so both "v11"s above load. *Rejected: whole-buffer hand
  migrations (a hand-written parser of the old layout per bump, forever, and no answer to two
  v11s); FlatBuffers (field ids merge silently wrong, its fixed structs cannot evolve at all, and it
  is a second source of truth beside the IDL for `Meshlet`/`VertexLayout`).*
- **ADR-3 — The current schema is a C++ builder beside each struct**, member pointers supplying
  offset and type, checked at registration for covering `sizeof(T)` and by the suite. *Rejected: a
  generator — same on-disk format, so it can replace the builder later without a migration.*
- **ADR-4 — Hooks predicate on the file's schema, never on the version number**, and a change of
  meaning renames the field. A hook keyed "below v10" would skip a sibling branch's v10 files that
  still carry the old shape; "the file's `SourceStamp` has a field named `mtime`" cannot. *Rejected:
  version-threshold hooks.*
- **ADR-5 — Load in memory; write current only on explicit save.** Opening a project never dirties
  it. `assetlib_cli migrate` rewrites a whole project deliberately. *Rejected: rewrite-on-open, which
  shows every asset modified after each pull; an editor prompt, which needs a policy for unattended
  runs anyway.*
- **ADR-6 — Unconvertible data throws one specific message**
  (`cube.bmesh: Submesh.material — file stores string, engine wants uint32, no conversion`); the
  editor shows the asset broken and the project opens. Every field declares a default, so "new field,
  old file" is never a failure. *Rejected: a silent placeholder; refusing the project.*
- **ADR-7 — All seven authored containers.** `.bvat` rides the container change because it shares
  `chunk_io.h`, but gets no fixture and no hook: it is derived, and an unreadable one is re-baked.
  `.ktx2` and `.bpak` are foreign or their own format and stay out. *Rejected: `.bmesh` and
  `.bmaterial` alone — five formats left positional to take the same bump later, one at a time.*
- **ADR-8 — One container format.** `.bmaterial`, `.bsky`, `.benvl` and `.benv` become chunked:
  their strings move to a string-pool chunk and each record becomes a fixed-layout POD with pool
  offsets, exactly as `.bmesh` stores its material paths. This reverses
  `libs/assetlib/CLAUDE.md`'s ".bmaterial is deliberately not one of them". *Rejected: a second,
  sequential schema mode for variable-length records — two converters to keep correct.*
- **ADR-9 — The schema table is chunk id 0**, and each chunk entry names its element type by index
  into it. `readChunks` already fetches chunks by id, so a survey costs one more small range read and
  the header does not change shape. *Rejected: a header field, which every reader would have to
  learn separately.*
- **ADR-10 — The schema lives in `assetlib`**, not `core`: `assetlib` is its only consumer.
  *Rejected: `core`, for a `bgl` or editor reader that does not exist — code goes in the layer of
  its lowest actual consumer.*
- **ADR-11 — The version number survives as a label** — bumped whenever a schema changes, printed
  by `describe`, and checked only one way: a file newer than the reader is refused. *Rejected:
  dropping it, which leaves nothing human-readable to tell files apart.*
- **ADR-12 — Conversion rules.** Same name and kind: copy. Integer or enum widening: convert.
  Nested struct: recurse. Fixed array: copy the shorter count, default the rest. Only in the current
  schema: its declared default. Only in the file: dropped. `Formerly("old")`: matched under the old
  name. Anything else — narrowing, float↔integer, struct↔scalar — is ADR-6's error. *Rejected:
  narrowing with a range check; a value that does not fit is data loss, and it should be a hook.*
- **ADR-13 — A hook is registered per container** with a predicate over the file's schema and a
  body that sees the old chunk bytes through the old schema, by name, beside the converted current
  struct. Hooks run after conversion, in registration order, and are kept indefinitely — each is
  small and self-gating. *Rejected: a hook per container version, or a retention rule — the first is
  ADR-4's mistake, the second decides on data nobody can see.*
- **ADR-14 — The committed fixtures cross with the task that ports their container**, by a local
  one-shot; the committed bytes are the artefact. Each port task also commits one file at its first
  self-describing schema, loaded by the suite forever, so a later schema edit that breaks old files
  fails CI rather than an editor. *Rejected: one throwaway program for today's positional formats,
  which is dead the day after it runs and would have to be maintained across the eight tasks.*
- **ADR-15 — `docs/specs/asset_container_migration.md` is deleted by the last task**; what outlives
  the feature moves to a new `docs/asset_schema.md`. *Rejected: deleting it in task 4, when the
  first container becomes self-describing — its "collapse, do not chain" and runner rules still guide
  task 7.*

## Non-goals

- A schema generator or IDL; the builder is the authoring form.
- Deriving meaning hooks automatically; the spec's finding stands — a differ sees shape, not value.
- Rewriting `bernini-test-project`: re-baked by hand, once. It will not open between the `.bmesh`
  port landing and that re-bake.
- An editor menu for bulk migration; the CLI is the deliberate path.
- Retiring hooks; endianness; cross-language readers; `.ktx2`, `.bpak`, `.bvat` migration.

## Acceptance

- `just test assetlib` — write with schema A, read with schema B, for every rule in ADR-12 and each
  ADR-6 error, one hook, all seven containers round-tripping through the store, and the two-branch
  case: two files that both claim v10 with different shapes both load, and a hook fires on the one
  whose schema says it needs it.
- One committed old-schema fixture per container under `assets/`, loaded by `assetlib_tests`.
- `just test` in full: `bgl_tests`, `gamelib_tests`, `editor_tests` and the examples load the same
  fixtures, so a broken port fails there too.

## What the survey found

- Two families. `.bmesh` (v3), `.bskel` (v1), `.banim` (v1), `.bvat` (v2) share
  [chunk_io.h](../../libs/assetlib/src/chunk_io.h): a 32-byte `Header`, 16-byte-aligned chunks of
  POD arrays, a 24-byte `Entry` table at the end. An absent chunk reads as empty; a changed
  `elementSize` throws (`chunk_io.cpp:81`), so growing `Submesh` is a hard bump today.
  `.bmaterial` (v10), `.bsky` (v2), `.benvl` (v2.1), `.benv` (v2) are positional `ByteWriter`
  streams with `writeString` (u32 length + bytes, [string_io.h](../../libs/assetlib/src/string_io.h));
  `.benvl` reads its tail only when `minor >= 1` (`benvl_io.cpp:71`) — the one two-shape reader.
- The PODs that reach disk, all in
  [assetlib_structs](../../libs/assetlib_structs/include/assetlib_structs/): `Node`, `Transform`
  (`Node.h`), `Mesh`, `Submesh`, `Meshlet`, `IndexType` (`Mesh.h`), `VertexLayout`,
  `VertexAttribute` and two `uint8_t` enums (`VertexLayout.h`), `Bone` (`Skeleton.h`),
  `AnimationClip` (`Animation.h`), `SourceStamp`, and `banim_io.cpp`'s private `SkeletonRef`. Every
  one has a `static_assert(sizeof)`. The flat structs `BMaterial`/`PbrParams`/`ChannelRoute`,
  `BSky`/`BEnvLighting`/`BEnv`/`EnvMapRoute` hold `std::string`, `std::array<…, 9>` and one
  `std::optional<float>`; they gain a POD disk record each.
- Partial reads. `loadMeshRefs`, `loadAnimationRefs`, `loadVatRefs` and the `.bvat` tables go
  through `chunk::readChunks*` (`bmesh_io.cpp:179`, `banim_io.cpp:147`, `bvat_io.cpp:307`), reading
  header, table and named chunks only; `asset_refs.cpp:58` and the editor's
  `animation_bindings.cpp:13` depend on that staying cheap.
- Refusal today is `"<what>: unsupported version N (expected M)"` in each flat reader and
  `"<what>: unsupported major version"` in `checkHeader` (`chunk_io.cpp:36`); nothing reads a
  version without demanding it match.
- Fixtures: `assets/Meshes/apples.bmesh` (re-bakeable from LFS `assets/apples.glb`),
  `assets/Materials/apples/Apple{1,2}.bmaterial`, `assets/Sky/forest.bsky`,
  `assets/EnvLighting/forest.benvl`, `assets/Environments/forest.benv` (sources absent; #371
  crossed them by script). Loaded by `assetlib_tests` (Describe, EnvContainers, EnvImport,
  FlatSeam, AssetRefs), `bgl_tests` (`TestEnvironment.cpp`), `gamelib_tests`
  (`AssetManager_test.cpp`), `editor_tests` (thumbnail caches) and `examples/bgl_base`,
  `bgl_sphere`.
- `core` has no field descriptor, reflection or member-pointer utility; `ByteReader`/`ByteWriter`
  are `memcpy` of a C++ type. `assetlib_cli` is one CLI11 `main.cpp` with twelve subcommands.
- Docs that state the layout: `docs/asset_standards.md:352-355` and `:418-422` ("exactly one
  readable version, and no migration path"), `docs/archives.md:135`, `libs/assetlib/CLAUDE.md`
  (chunk paragraph), and the `@throws` lines of `bmesh_io.h`, `banim_io.h`, `bvat_io.h`.

## What changes

- `libs/assetlib` — new `schema/`: the descriptor, the builder, the schema table's byte form, the
  converter and its errors, the hook registry. `chunk_io.h` gains the schema chunk, typed entries,
  conversion on `Read`/`Require`, and the newer-than-reader guard; the four flat serializers become
  chunk writers over new record PODs; the eight `c_VersionMajor` constants stay as labels. Every
  test that constructs a container by hand keeps working through the same `serialize`/`load` calls.
- `libs/assetlib/cli/main.cpp` — `migrate`.
- `apps/editor` — the thumbnail cache and Content Explorer show an unreadable asset's message
  instead of a `qWarning`.
- `assets/` — every fixture re-saved once; one old-schema fixture per container added.
- `docs/` — `asset_schema.md` new; `asset_standards.md`, `archives.md`, `libs/assetlib/CLAUDE.md`
  corrected; the spec and this plan deleted at the end.
- Could break: anything that read a container as bytes with knowledge of the layout —
  `asset_describe.cpp`, `asset_rename.cpp`, `texture_prune.cpp`, `bmesh_texture.cpp` — and every
  suite that loads a fixture. `bernini-test-project` is unreadable until re-baked.

## Tasks

1. **The plan** — this file. Gate: review.
2. **`feat(assetlib): describe a POD's layout — the schema, its builder, its byte form`** — the
   descriptor types, the builder from member pointers, the completeness check, and the schema
   table serialized and parsed back. Nothing reads it yet; the tests do. Gate: `[schema]` tests —
   every disk POD's builder covers its `sizeof`, a table round-trips byte-exact.
3. **`feat(assetlib): convert bytes between two schemas by field name`** — ADR-12 and ADR-6.
   Gate: one test per rule, one per error, on synthetic schemas.
4. **`feat(assetlib): the chunk container carries its schema, and reads any older one`** — the
   schema chunk, typed entries, conversion inside `Reader`, `readChunks` fetching the schema, hooks,
   the newer-than-reader guard; `.bmesh`, `.bskel`, `.banim`, `.bvat` on it; `apples.bmesh` re-baked
   from `apples.glb`; old-schema `.bmesh`/`.bskel`/`.banim` fixtures committed; the two-branch test.
   Gate: `just test assetlib gamelib editor bgl`.
5. **`feat(assetlib): .bmaterial as a self-describing chunk container`** — `MaterialRecord`,
   `PbrRecord` and the pool; `Apple{1,2}` re-saved; the old-schema fixture; describe, prune, bake
   and staleness untouched in behaviour. Gate: `just test assetlib editor`.
6. **`feat(assetlib): .bsky, .benvl and .benv as self-describing chunk containers`** — the env
   route record; `.benvl`'s minor branch retired; `forest.*` re-saved; old-schema fixtures. Gate:
   `just test assetlib bgl gamelib`.
7. **`feat(assetlib): assetlib_cli migrate`** — a pure `migrateProject` (walk, `--dry-run`, the
   second walk authoritative, an unchanged file never rewritten, unconvertible files reported) and
   the subcommand. Gate: tests on a temp project — idempotent on the second run, reports the file it
   cannot convert, touches nothing on `--dry-run`.
8. **`feat(editor): an asset that cannot be read says why`** — the message from ADR-6 reaches the
   thumbnail and the Content Explorer. Gate: `editor_tests` — an unconvertible fixture shows its
   message; the project opens.
9. **`docs(assetlib): the schema, and the containers on it`** — `docs/asset_schema.md`, the
   corrections above, `docs/specs/asset_container_migration.md` and this plan deleted. Gate: review.
