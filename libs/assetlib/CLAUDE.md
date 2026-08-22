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

Three container regimes, mid-migration (see docs/plans/migration-system-v2.md):

`.bmaterial` is an **authored text document**: canonical JSON (`src/bmaterial_io.cpp`), factors and
routes as named keys, the editor graph carried as an opaque string, unknown keys preserved on
round-trip so a sibling branch's field survives a reader that has never heard of it. The chunk-era
form still deserializes — `migrate` is the carry — until the schema system goes.

`.bmesh`, `.bskel` and `.banim` are **cache entries**, in `src/cache_io.h`: a frozen header carrying
the cache key (bake token, source stamp, parameter hash), raw current-layout chunks with no
self-description, and a chunk table. A chunk is addressed by id and an absent one is not an error.
There is no conversion and no old shape to parse — a token mismatch is a cache miss, and
`AssetStore`'s `LoadRegen*` methods are the seam that acts on one: a stale entry regenerates in
memory from its `meshes_src/` source at the parameters its `.bimport` records, with the document's
bindings applied over the result, while a read-only store trusts its keys because `pack` made them
true. A change to what these containers store — layout or meaning — is one edit: bump the
container's token in `src/bake_tokens.h` to a fresh random value.

`.bvat` and the env containers still ride the schema format in `src/chunk_io.h`: chunk 0 is the
file's schema, a reader converts each chunk by field name, and a change of *meaning* is a
`chunk::Hook` whose predicate reads the file's schema, never its version. They convert to their own
regimes (documents, cache entries) in later tasks of the plan.

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
