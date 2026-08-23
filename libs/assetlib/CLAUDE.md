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

That licence is for the reference, not for everything beside it. `posedBounds` is a *derived* box
rather than a skin, so it takes the cheap shape a shipping engine takes — a box per bone swept by
the pose — and `exactPosedBounds` is the per-vertex walk it is proved against. See
[docs/skinning.md](../../docs/skinning.md).

Two container regimes (see docs/asset_containers.md):

`.bmaterial` and `.benv` are **authored text documents**: canonical JSON (`src/bmaterial_io.cpp`,
`src/benv_io.cpp`), named keys, unknown keys preserved on round-trip so a sibling branch's field
survives a reader that has never heard of it. `.benv` carries the env family's authored state —
the composition and the presentation knobs (`skyMipLevel`, `skyRotationY`, `exposureOverride`).

Everything else derived — `.bmesh`, `.bskel`, `.banim`, `.bvat`, `.bsky`, `.benvl` — is a **cache
entry**, in `src/cache_io.h`: a frozen header carrying the cache key (bake token, source stamp,
parameter hash, source mount key), raw current-layout chunks with no self-description, and a chunk
table. A chunk is addressed by id and an absent one is not an error. There is no conversion and no
old shape to parse — a token mismatch is a cache miss. For geometry, `AssetStore`'s `LoadRegen*`
methods are the seam that acts on one: a stale entry regenerates in memory from its `meshes_src/`
source at the parameters its `.bimport` records, with the document's bindings applied over the
result, while a read-only store trusts its keys because `pack` made them true. For the env family
and `.bvat` the re-bake is deliberate (`pack`, the editor) rather than at load. A change to what a
container stores — layout or meaning — is one edit: bump its token in `src/bake_tokens.h` to a
fresh random value. A forgotten bump on a layout change fails `TokenCanary_test`, which pins each
writer's output hash beside its token; a semantic change the fixture cannot see is still yours to
remember.

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
- **A new container type is a new `AssetCodec` specialization**, declared beside its io and listed
  in `Containers` in `src/container_table.cpp` — which is what `containerKinds()` is built from and
  what a static assertion holds to `AssetType`. The extension, the type enum and the bake token
  are still spelled out in several older places as well; collapsing those onto the table is the
  rest of [docs/specs/assetlib_store_codecs.md](../../docs/specs/assetlib_store_codecs.md), which
  is in progress. Until it lands, adding a container means editing both, and the table's assertion
  is what stops you forgetting the half that has no compiler behind it.
- **A project's asset is read and written by key, through the store**: `store.Load<T>(key)` and
  `store.Save(value, key)`. The `save*`/`load*` functions taking a `std::filesystem::path` are the
  older surface and are being removed; do not add a caller.

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
