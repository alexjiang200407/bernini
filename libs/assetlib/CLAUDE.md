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

Two container regimes, mid-migration (see docs/plans/migration-system-v2.md):

`.bmesh`, `.bskel` and `.banim` are **cache entries**, in `src/cache_io.h`: a frozen header carrying
the cache key (bake token, source stamp, parameter hash), raw current-layout chunks with no
self-description, and a chunk table. A chunk is addressed by id and an absent one is not an error.
There is no conversion and no old shape to parse — a token mismatch is a cache miss, and the file's
only future is regeneration from its `meshes_src/` source. A change to what these containers store —
layout or meaning — is one edit: bump the container's `c_BakeToken` to a fresh random value.

`.bvat`, `.bmaterial` and the env containers still ride the schema format in `src/chunk_io.h`:
chunk 0 is the file's schema, a reader converts each chunk by field name, and a change of *meaning*
is a `chunk::Hook` whose predicate reads the file's schema, never its version. They convert to
their own regimes (text documents, cache entries) in later tasks of the plan.

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
