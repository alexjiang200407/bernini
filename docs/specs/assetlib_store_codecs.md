# One store, one codec per container

`AssetStore` owns the read half of a project and nothing of the write half. This is the design
that finishes it. **It is not built**, and it deliberately waits — see the trigger below.

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

**The container list is written out six times**: the extensions in
[container_format.h](libs/assetlib/include/assetlib/container_format.h), `assetTypeFromExtension`
in [asset_refs.cpp](libs/assetlib/src/asset_refs.cpp), the magic switch in `assetlib_cli`'s
`sniff`, and the switches in [migrate.cpp](libs/assetlib/src/migrate.cpp) and
[asset_rename.cpp](libs/assetlib/src/asset_rename.cpp) — plus the twelve hand-written `Load*`
methods on the store itself. A new container is seven edits in lockstep.

The cause is that the store arrived after the free functions did, and only the operations written
*since* were given it: `packProject`, `findUnusedBakedTextures`, `bakeVat` and
`AssetRefGraph::Scan` all take a `const AssetStore&` today. The library is mid-migration and has
been left there.

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

So one specialization per container, beside that container's io:

```cpp
template <> struct AssetCodec<BMesh>
{
	static constexpr std::string_view Extension = c_MeshExtension;
	static constexpr uint32_t         Magic     = magic::c_BMesh;

	static std::vector<std::byte> Serialize(const BMesh&);
	static BMesh                  Deserialize(std::span<const std::byte>);
};
```

and two templates on the store, replacing twelve `Load*` methods and every `save*` free function:

```cpp
auto mesh = store.Load<BMesh>("Meshes/a.bmesh");
store.Save(mesh, "Meshes/a.bmesh");
```

The key never becomes a path in caller code, which is the whole point.

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

The `load`/`save`-by-`path` pair is **kept** for files no project owns: `assetlib_cli obj` writing
a `.obj` to an arbitrary directory, a standalone baked model directory that is its own data root,
and `writeObj`'s debugging output. [STYLE.md](STYLE.md) § Paths already draws this line — a
`std::filesystem::path` is a location on the host, a `string_view` is a key into a mount. What
changes is that a *project's* asset stops being addressable the first way.

## What this is not

* **Not an on-disk format change.** Byte-identical containers, no version bump, no re-bake.
  Purely the C++ surface.
* **Not a rewrite of the chunk layer.** `chunk_io.h` and the schema conversion
  ([docs/asset_schema.md](docs/asset_schema.md)) are what the codecs call, unchanged.
* **Not an editor restructuring.** Call sites in `apps/editor` change mechanically because their
  callee changed; nothing else there is in scope.
* **Not runtime-registrable.** There is no plugin story and no need for one — every container this
  engine reads is compiled in.

## The trigger

Build this **once `feat/migration-system-v2` has landed.**

That feature is a live ~3,700-line rewrite of exactly these files — `asset_import`, `bmesh_io`,
`banim_io`, `bskel_io`, `container_info`, `asset_refs`, `asset_rename`, `migrate`, plus a new
`AssetStore_Regen.cpp`, `cache_io` and `import_document`. Landing this restructure first would put
that work through a rebase onto a rewritten library for no gain: the asymmetry has been tolerable
for months and will keep.

The second trigger, if it comes first: **a seventh place needing the container list**, or a new
container type. That is the point at which the lockstep edit stops being an annoyance and starts
being how a container gets half-registered.

## Acceptance

* A round trip through the store for **every** container type — `Save<T>` then `Load<T>` by the
  same key, byte-compared against the pre-refactor file, so "no format change" is asserted rather
  than asserted-in-prose.
* A test that a key containing a `\` is refused rather than silently resolving loose — the failure
  this removes, which nothing currently pins.
* `assetlib_cli` `describe`, `migrate` and `pack` behave identically on the test project.
* `just test assetlib gamelib editor` green.
