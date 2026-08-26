# assetlib

assetlib is a static library that contains a set of asset-related utilities. These include:

- Parsing glTF (.glb / .gltf, via tinygltf) and retrieving all the assets — geometry, materials,
  textures, and the skin and animations, which are cooked into `.bskel` / `.banim` beside the mesh
- Provide shared structures that both bgl and editor can use to communicate

Pose evaluation and CPU skinning live here, not in `bgl`: `poseModelTransforms` walks a clip frame
from local into model space, `skinningMatrices` composes each with its inverse bind
(`include/assetlib/skinning.h`), and `skinSubmesh` blends four influences per vertex
(`include/assetlib/skinning.h`). The bake is offline and assetlib never links `bgl`, so this is
plain CPU code — and it is the reference every later GPU path is diffed against, which is why it is
deliberately the unoptimised form.

That licence is for the reference, not for everything beside it. `posedBounds` is a *derived* box
rather than a skin, so it takes the cheap shape a shipping engine takes — a box per bone swept by
the pose — and `exactPosedBounds` is the per-vertex walk it is proved against. See
[docs/skinning.md](../../docs/skinning.md).

Two container regimes (see docs/asset_containers.md):

`.bmaterial`, `.benv` and `.bimport` are **authored text documents**: canonical JSON
(`src/bmaterial_io.cpp`, `src/benv_io.cpp`, `src/import_document.cpp`), named keys, unknown keys preserved on round-trip so a sibling branch's field
survives a reader that has never heard of it. `.benv` carries the env family's authored state —
the composition, the presentation knobs (`skyMipLevel`, `skyRotationY`, `exposureOverride`) and the
rim light — behind a `shadingModel`, the way `.bmaterial` does, so what only a PBR surface reads
sits in a block of its own.

Everything else derived — `.bmesh`, `.bskel`, `.banim`, `.bvat`, `.bsky`, `.benvl` — is a **cache
entry**, in `src/cache_io.h`: a frozen header carrying the cache key (bake token, source stamp,
parameter hash, source mount key), raw current-layout chunks with no self-description, and a chunk
table. A chunk is addressed by id and an absent one is not an error. There is no conversion and no
old shape to parse — a token mismatch is a cache miss. For geometry, `AssetStore`'s `LoadRegen*`
methods are the seam that acts on one: a stale entry regenerates in memory from its `Authored/Meshes/`
source at the parameters its `.bimport` records, with the document's bindings applied over the
result, while a read-only store trusts its keys because `pack` made them true. For the env family
and `.bvat` the re-bake is deliberate (`pack`, the editor) rather than at load. The textures an
import extracted are the third case: keyed by the `textureDir` and `textureStamp` their `.bimport`
carries, because a `.ktx2` has no header of its own, and refreshed by
`AssetStore::RefreshImportedTextures` rather than at load — `LoadRegen*` runs on every mesh load and
every deletion's reference scan, and an import's worth of Basis encoding cannot go there. A change
to what a container stores — layout or meaning — is one edit: bump `AssetCodec<T>::c_BakeToken`
(`include/assetlib/codecs.h`) to a fresh random value, beside the writer it has to move with. A
forgotten bump on a layout change fails `TokenCanary_test`, which pins each
writer's output hash beside its token; a semantic change the fixture cannot see is still yours to
remember.

The public surface is documented as a map in [docs/assetlib_api.md](../../docs/assetlib_api.md).

## The bar here is the strict one

assetlib and `assetlib_cli` are held to the same bar as `bgl`: the headers under `include/` are
what a reader learns this library from, so there is one seam per concern and one place each rule
lives. `apps/editor` gets a looser bar; this does not — see
[the root CLAUDE.md](../../CLAUDE.md) § The bar each subsystem is held to.

Concretely, before adding to `include/assetlib/`:

- **A project's asset is addressed by a mount key, through `AssetStore`**: `store.Load<T>(key)`
  and `store.Save(value, key)`. A new function taking a `std::filesystem::path` to a file the
  project owns is the second way to do a thing that already has one, and the family that did that
  was deleted rather than kept — see [STYLE.md](../../STYLE.md) § Paths.
- **An operation on a project is a method on `AssetStore`; an operation with state that outlives
  the call is a class.** A free function taking an `AssetStore&` is a method that has not been
  written as one -- `bakeVat(store, desc)` sat beside `store.BakeMaterial` for exactly as long as
  nobody noticed. The exception is real and narrow: `AssetRefGraph::Scan(store)` builds an object
  holding an edge index across every later `ReferrersOf`, so it is a named constructor and stays
  free. Stateless and about the project's contents means method.
- **A container is written into the half its codec belongs to, and `Save` refuses anything else.**
  `CacheEntryCodecFor<T>` decides which half; `requireOrigin` (`include/assetlib/project_layout.h`)
  is where the refusal lives, and the untyped destinations that reach the host directly --
  `WriteTextures`' `textureDir`, `EnvImportDesc`'s directories -- call it themselves. Do not add a
  parameter letting a caller name a directory a bake writes into: the routes a bake stores are
  references, and one outside `Derived/` is a reference the split does not cover. That is why the
  bakes take no `textureDir` any more.
- **A caller never creates a directory for a store write.** `store.Save(value, key)` creates what
  the key names; a key is a location in the data root, not one that already exists. A caller that
  writes straight to the host is the exception and looks different — `writeKTX2` and `copy_file`
  make no directory, so those callers still make their own.
- **A caller that genuinely addresses the host encodes and moves bytes itself**, so it cannot be
  mistaken for a project write: `AssetCodec<T>::Serialize` plus `core::file::write_atomic`. That
  is `assetlib_cli strip --out` writing a shipping tree, and the editor opening a mesh from
  outside any data root. Both are real; neither is a reason to bring the old family back.
- **Do not re-carry a data root.** `AssetStore`'s two constructors are the only *declarations* in
  `include/assetlib` that take one; the word appears elsewhere only in prose, saying what a path is
  relative to. `MaterialBakeDesc`, `EnvBakeDesc`, `ImportTarget` and `EnvImportDesc` all carried one
  as a member; a descriptor that names *what* to write does not also get to say *where*.
- **A new container type is a new `AssetCodec` specialization** in `include/assetlib/codecs.h`,
  listed in `Containers` in `src/container_table.cpp`. That is the whole registration: `containerKinds()`
  is folded out of it, and a static assertion holds the list to `AssetType`, so a type added to the
  enum and forgotten in the tuple does not compile. The assertion anchors on `AssetType::kCount`
  rather than the last enumerator — anchoring it on the latter meant *appending* a type satisfied
  it silently, which is the case it exists to catch.
- **Behaviour per container is a `switch`, not a table entry.** `migrate`, `asset_rename` and
  `pack` do different work per type, and each switch is exhaustive with no `default:` so
  `-Wall -Werror` turns a new `AssetType` into a compile error there. Do not add a `default:` to
  one; that is the guarantee.

## Headers forward declare

A header forward declares the types it names from its **own** namespace — `BMesh`, `BMaterial`,
`SourceStamp`, `ImageData`, all of them `assetlib::` — instead of including `assetlib_structs`.
Include the definition only where one is genuinely required: `envmap.h` includes `ImageData.h`
because `EnvironmentMaps` holds three of them by value, and a reference or a by-value return does
not.

Any other namespace is included, and *exactly* the same namespace is what counts. `assetlib::imp`
is not `assetlib`, so `imp::BMeshImport` is included rather than declared — reopening a nested
namespace to declare one struct spreads knowledge of someone else's layout, and `core::` and
`std::` are included for the same reason.

The corollary is that a `.cpp` includes the structs it uses. Nothing picks a definition up
transitively through an `assetlib/` header, so an include here names what the file actually needs.
