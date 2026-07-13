# gamelib

gamelib provides game-related abstractions built on top of the renderer and the asset layer. It is the
**seam**: the only library allowed to link both `bgl` and `assetlib`.

- CMake target: `gamelib` (static). CMake: `./CMakeLists.txt`
- Namespace: `game`
- Verification: `gamelib_tests`

## Why it exists

The two libraries below it are deliberately kept apart:

- `bgl` links `assetlib_structs` (POD headers) but **never** `assetlib`. Image decoding lives in the
  asset library; graphics code stays codec-free and consumes decoded `ImageData` through
  `IScene::AddTextureAsset`.
- `assetlib` **never** links `bgl`. It is the offline cook library, and `assetlib_cli` uses it — a
  command-line baker must not pull in a D3D12 renderer.

Anything that needs both — "read this `.bmaterial` off disk and give me a `MaterialHandle`" — belongs
here, not in either of them.

## Contents

- `AssetManager` — constructed with an `ISceneView` and the project's **Data directory**. Every asset
  reference Bernini stores is a path relative to that root, so it is supplied once at construction
  rather than threaded through every call.

### AssetManager: identity is the path, lifetime is a reference count

**Identity.** A path maps to one texture upload and one material, however many times it is asked for.
Geometry is keyed by `path#meshIndex`, because a `.bmesh` holds several meshes. Cubes and spheres have
no file, so they are not shared — but they are refcounted like anything else.

**Lifetime.** References run along the edges the assets themselves have:

```
instance -> geom -> material -> texture
         \-> material (per-submesh override, when one is worn)
```

`AcquireMesh` acquires the materials its submeshes name, which acquires the textures those materials
name; `Release*` runs the chain in reverse and destroys only at zero.

That is not just tidy — **it is what makes deletion safe**. `bgl` deliberately tracks nothing, and
documents preconditions it cannot check: a material may not be deleted while a submesh is bound to it,
a texture may not be deleted while a material routes it, and geometry may not be deleted while an
instance references it (`IScene::DeleteGeom`). A reference count of zero *means* exactly those things.
The manager owns instances for that last one: an instance holding a reference on its geom is what makes
"the last reference is gone" imply "nothing is drawing it".

**Swapping.** `SetSubmeshMaterial` rebinds a submesh (acquiring the new material, releasing the old).
`SetMaterialTexture` / `SetMaterialRoute` swap a map on a live material: the scene rewrites the entry
in place (`IScene::UpdatePbrMaterial`), so the handle stays valid and every submesh bound to it follows
without being rebound. The material is shared by path, so the change is seen by everything using it.

**Skins.** `SetSubmeshMaterial` changes a geom's **default**, so it reaches every instance placed from
it. `SetInstanceSubmeshMaterial` overrides **one instance** and leaves its siblings alone — the same
unit mesh, a different material per unit. The override outranks the default and holds a reference of
its own, which is the edge above: `ClearInstanceSubmeshMaterial` and `DestroyInstance` release it.
Without that reference `bgl` would happily let the material be deleted out from under an instance
still wearing it, since a binding there is a bare slot index with no generation
(`ISceneView::SetSubmeshMaterialOverride`).
