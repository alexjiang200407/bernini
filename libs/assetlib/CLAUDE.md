# assetlib

assetlib is a static library that contains a set of asset-related utilities. These include:

- Parsing glTF (.glb / .gltf, via tinygltf) and retrieving all the assets — geometry, materials,
  textures, and the skin and animations, which are cooked into `.bskel` / `.banim` beside the mesh
- Provide shared structures that both bgl and editor can use to communicate

Pose evaluation and CPU skinning live here, not in `bgl`: `poseModelTransforms` walks a clip frame
from local into model space, `skinningMatrices` composes each with its inverse bind
(`include/assetlib/skeleton.h`), and `skinSubmesh` blends four influences per vertex
(`include/assetlib/skinning.h`). The bake is offline and assetlib never links `bgl`, so this is
plain CPU code — and it is the reference every later GPU path is diffed against, which is why it is
deliberately the unoptimised form.

`.bmesh`, `.bskel`, `.banim` and `.bvat` are one chunked container format, in `src/chunk_io.h`. A chunk is
addressed by id and an absent one is not an error. Chunk 0 is the file's schema (`schema`):
every POD a chunk holds is registered as a layout — the shared ones as `AssetSchemaBuilder`'s chain, a
container's private ones beside its io — and a reader converts each chunk from the layout the file
stores to the current one by field name, so a struct that changed shape leaves old files readable.
A change of *meaning* is a `chunk::Hook` whose predicate reads the file's schema, never its version;
rename the field when its meaning changes, so the schema can see it. `.bmaterial` is one of them too:
its strings live in a pool chunk and its records are PODs with pool offsets, so one converter serves
every container.

The public surface is documented as a map in [docs/assetlib_api.md](../../docs/assetlib_api.md).

## The bar here is the strict one

assetlib and `assetlib_cli` are held to the same bar as `bgl`: the headers under `include/` are
what a reader learns this library from, so there is one seam per concern and one place each rule
lives. `apps/editor` gets a looser bar; this does not — see
[the root CLAUDE.md](../../CLAUDE.md) § The bar each subsystem is held to.

Concretely, before adding to `include/assetlib/`:

- **A project's asset is addressed by a mount key, through `AssetStore`.** A new function that
  takes a `std::filesystem::path` to a file the project owns is adding the second way to do a
  thing that already has one. `std::filesystem::path` is for files no project owns — see
  [STYLE.md](../../STYLE.md) § Paths.
- **Do not re-carry a data root.** `MaterialBakeDesc` and `EnvBakeDesc` do, and they are the
  standing example of what not to copy; the store already holds it.
- **A new container type is not a new switch.** The extension, the magic and the type enum are
  already spelled out in several places, and
  [docs/specs/assetlib_store_codecs.md](../../docs/specs/assetlib_store_codecs.md) is the design
  that collapses them. Read it before adding the next one.

## Headers forward declare

A header forward declares the types it names from its **own** namespace — `BMesh`, `BMaterial`,
`SourceStamp`, `ImageData`, all of them `assetlib::` — instead of including `assetlib_structs`.
Include the definition only where one is genuinely required: `benv_io.h` includes `ImageData.h`
because `EnvironmentMaps` holds three of them by value, and a reference or a by-value return does
not.

Any other namespace is included, and *exactly* the same namespace is what counts. `assetlib::imp`
is not `assetlib`, so `imp::BMeshImport` is included rather than declared — reopening a nested
namespace to declare one struct spreads knowledge of someone else's layout, and `core::` and
`std::` are included for the same reason.

The corollary is that a `.cpp` includes the structs it uses. Nothing picks a definition up
transitively through an `assetlib/` header, so an include here names what the file actually needs.
