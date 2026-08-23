# One store, one codec per container

`AssetStore` owns the read half of a project and nothing of the write half. This is the design
that finishes it. **It is not built**, and as of #463 nothing is holding it back — see the trigger
below.

## The problem

Three symptoms, one cause.

**The seam runs one way.** A read is `store.LoadMesh("Meshes/a.bmesh")` — a mount key, resolved
through the mount. A write is `save(mesh, store.ResolveWritePath("Meshes/a.bmesh"))`, or, in the
editor, `saveMaterial(material, dataRoot / relative)` with the join done by hand
([apps/editor/src/Import/import_writers.cpp](apps/editor/src/Import/import_writers.cpp),
[apps/editor/src/Windows/MaterialEditor/MaterialEditorWindow.cpp](apps/editor/src/Windows/MaterialEditor/MaterialEditorWindow.cpp)).
Every write site restates the rule that a key becomes a path exactly one way, and a site that
restates it with `std::filesystem::path` instead builds a `\`-separated key that resolves loose
and misses packed, silently ([STYLE.md](STYLE.md) § Paths).

**Two structs re-carry half a store.** `MaterialBakeDesc` and `EnvBakeDesc` are both
`{ dataRoot, textureDir }` — identical, and both a `dataRoot` the store already holds
([material_bake.h](libs/assetlib/include/assetlib/material_bake.h),
[env_bake.h](libs/assetlib/include/assetlib/env_bake.h)).

**The container list is written out seven times**: the extensions in
[container_format.h](libs/assetlib/include/assetlib/container_format.h), `assetTypeFromExtension`
in [asset_refs.cpp](libs/assetlib/src/asset_refs.cpp), the magic switch in `assetlib_cli`'s
`sniff`, the switches in [migrate.cpp](libs/assetlib/src/migrate.cpp) and
[asset_rename.cpp](libs/assetlib/src/asset_rename.cpp), the pack rules in
[pak_pack.cpp](libs/assetlib/src/pak_pack.cpp), and — added by #463 — the token table in
[bake_tokens.h](libs/assetlib/src/bake_tokens.h). Plus the twenty `Load*` methods on the store
itself. A new container is eight edits in lockstep.

The cause is that the store arrived after the free functions did, and only the operations written
*since* were given it: `packProject`, `findUnusedBakedTextures`, `bakeVat` and
`AssetRefGraph::Scan` all take a `const AssetStore&` today. The library is mid-migration and has
been left there.

**#463 widened all three rather than narrowing any.** The source+cache model added six
`LoadRegen*` methods to the store's read half and no write half at all, so the load surface went
from twelve methods to twenty against zero saves; `bake_tokens.h` became the seventh place the
container list is spelled out; and both bake descs are untouched. The diagnosis below is the
diagnosis from before that change, and every line of it still holds.

## The standard

Every shipping engine converges on the same shape: **one façade addressed by a virtual path,
per-format codecs in a registry, and the façade owns both directions.**

* **Godot** — `ResourceFormatLoader` and `ResourceFormatSaver`, registered with `ResourceLoader` /
  `ResourceSaver`, dispatched by extension and type.
* **Unity** — `AssetDatabase.LoadAssetAtPath` / `CreateAsset`, with a `ScriptedImporter` per
  extension.
* **Unreal** — `UFactory` for import and `FArchive` package serialization; nothing addresses a
  raw OS path, everything goes through `/Game/…` package paths.

`AssetStore` is that façade, half-built. Finishing it is adopting the standard, not inventing.

## The design

### A codec is a compile-time trait, not a registered object

This is a **named deviation** from the standard shape above. Godot, Unity and Unreal can all use a
runtime-polymorphic registry because everything they load derives from one base — `Resource`,
`Object`, `UObject`. Our containers are unrelated PODs: `BMesh`, `BMaterial`, `BSky` and
`Skeleton` share no base and never will. A virtual `IAssetCodec` would have to return `std::any`
or a variant, so every call site casts back, and it fights `ROADMAP.md` § Guiding Constraints —
*Data-Oriented Design; traditional OOP will decimate your CPU cache.*

So one specialization per container, beside that container's io. #463 split the containers into
two families — authored **documents** (`.bmaterial`, `.benv`, `.bimport`, canonical JSON) and
derived **cache entries** (`.bmesh`, `.bskel`, `.banim`, `.bvat`, `.bsky`, `.benvl`, a frozen key
header over chunks) — but both kept the same `serialize`/`deserialize`-over-bytes shape, so one
trait still serves both. What separates them is a bake token, which only a cache entry has:

```cpp
template <> struct AssetCodec<BMesh>
{
	static constexpr std::string_view Extension = c_MeshExtension;
	static constexpr uint32_t         Magic     = magic::c_BMesh;

	// Cache entries only; a document has no bake token and declares none.
	static constexpr uint64_t BakeToken = c_BMeshBakeToken;

	static std::vector<std::byte> Serialize(const BMesh&);
	static BMesh                  Deserialize(std::span<const std::byte>);
};
```

Folding `bake_tokens.h` in here is the point rather than a detail: a token exists to be bumped
whenever a writer's layout changes, and it currently lives one file away from the writer it
describes. Beside `Serialize`, the thing it has to move with is on the next line.

Then two templates on the store, replacing twenty `Load*` methods and every `save*` free function:

```cpp
auto mesh = store.Load<BMesh>("Meshes/a.bmesh");
store.Save(mesh, "Meshes/a.bmesh");
```

The key never becomes a path in caller code, which is the whole point.

The `LoadRegen*` family stays a separate seam and is **not** absorbed. Regeneration is not a
decode: it re-cooks from a copied source on a cache miss, which is a different operation with a
different failure mode, and a caller asks for it deliberately.

### One registration list feeds both halves

The runtime questions are real and stay: `sniff` reads a magic because `describe` must work on a
file named anything; `migrate` walks every authored container; `rename` must know what kind of
file it is rewriting; `pack` must know what belongs in an archive. Those get a table — but a
**generated** one, from a single list of the codec specializations, so the seven edits become one.
Nothing dispatches dynamically that does not have to.

### The store owns both directions; paths survive where a project does not

`AssetStore` gains `Save<T>`, absorbs `MaterialBakeDesc` and `EnvBakeDesc` (they become the store
plus an optional `textureDir`), and the `save*(x, std::filesystem::path)` free functions are
deleted.

The `load`/`save`-by-`path` pair is **deleted outright**, and this reverses what this spec said
when it was written. It kept the pair for "files no project owns" — `assetlib_cli obj`, a
standalone baked model directory, `writeObj`'s output. Counting the callers before building it
found that carve-out has **zero users**: all 76 production call sites address a file inside a
project, the CLI's `out` paths included, since those already resolve through `ResolveWritePath`. An
escape hatch nobody uses is the second way to do one thing that [CLAUDE.md](CLAUDE.md) § The bar
each subsystem is held to forbids, so it goes.

`writeObj`, `loadFromGltf`, `Project::Open` and `packProject` keep their `path` parameters —
they address the host, which is a different question.

One caller genuinely writes a container outside a project: `assetlib_cli strip --out`, whose own
help says *"a shipping tree is not a project"*. Counting call sites missed it, because that site
has two modes and only the non-default escapes. It encodes with the codec and writes the bytes
itself — `core::file::write_atomic(out, AssetCodec<BMaterial>::Serialize(material))` — which is the
shape such a caller should have: writing bytes to a path a user named is not saving a project's
asset, and making the two look alike is what put the family here in the first place. [STYLE.md](STYLE.md) § Paths draws that line:
a `std::filesystem::path` is a location on the host, a `string_view` is a key into a mount. What
changes is that a *project's* asset stops being addressable the first way at all.

## What this is not

* **Not an on-disk format change.** Byte-identical containers, no version bump, no re-bake.
  Purely the C++ surface.
* **Not a rewrite of the container layer.** `src/cache_io.h` and the document seam
  ([docs/asset_containers.md](docs/asset_containers.md)) are what the codecs call, unchanged.
* **Not an editor restructuring.** Call sites in `apps/editor` change mechanically because their
  callee changed; nothing else there is in scope.
* **Not runtime-registrable.** There is no plugin story and no need for one — every container this
  engine reads is compiled in.

## The trigger — both have fired

This waited on two things, and #463 (`feat/migration-system-v2`, the source+cache asset model)
tripped both on 2026-08-23.

**The blocker is gone.** The wait was for a live ~3,700-line rewrite of exactly these files —
`asset_import`, `bmesh_io`, `banim_io`, `bskel_io`, `container_info`, `asset_refs`,
`asset_rename`, `migrate`, plus `AssetStore_Regen.cpp`, `cache_io` and `import_document`. Landing
the restructure first would have put that work through a rebase for no gain. It has landed.

**And the second trigger fired with it.** That trigger was *a seventh place needing the container
list*: `bake_tokens.h` is now that seventh place. It is also the sharpest example of why the
lockstep edit is a hazard rather than an annoyance — a token exists to be bumped when a writer's
layout changes, so a container registered in six places and missed in the seventh is one whose
stale files are read as current. `TokenCanary_test` exists precisely because that edit is easy to
forget, which is a test compensating for a shape the codec trait would make impossible.

Nothing is holding this back. Cut it from `master` as its own change, refactor-first, ahead of
whatever needs it.

## Acceptance

* A round trip through the store for **every** container type — `Save<T>` then `Load<T>` by the
  same key, byte-compared against the pre-refactor file, so "no format change" is asserted rather
  than asserted-in-prose.
* A test that a key containing a `\` is refused rather than silently resolving loose — the failure
  this removes, which nothing currently pins.
* `TokenCanary_test` passes **unchanged**. Moving a token into its codec must not move the value,
  and that suite is what proves the writers still emit what their tokens claim.
* `assetlib_cli` `describe`, `migrate` and `pack` behave identically on the test project.
* `just test assetlib gamelib editor` green.
