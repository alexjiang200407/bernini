# gamelib

gamelib provides game-related abstractions built on top of the renderer and the asset layer. It is the
**seam**: the only library allowed to link both `bgl_extended` and `assetlib`.

- CMake target: `gamelib` (static). CMake: `./CMakeLists.txt`
- Namespace: `game`
- Verification: `gamelib_tests`

## Why it exists

The two libraries below it are deliberately kept apart:

- `bgl_extended` links `assetlib_structs` (POD headers) but **never** `assetlib`. Image decoding lives in the
  asset library; graphics code stays codec-free and consumes decoded `ImageData` through
  `IScene::AddTextureAsset`.
- `assetlib` **never** links `bgl_extended`. It is the offline cook library, and `assetlib_cli` uses it — a
  command-line baker must not pull in a D3D12 renderer.

Anything that needs both — "read this `.bmaterial` off disk and give me a `MaterialHandle`" — belongs
here, not in either of them.

## Contents

- `Raycaster` — CPU picking: register geometry (a `BMesh`'s meshlet streams, or an analytic
  sphere for procedural shapes), place instances of it, and ask which (instance, submesh) a
  world-space `game::Ray` meets first. It keeps its own compact copy of positions and triangles,
  because nothing retains CPU geometry after the GPU upload — feed it while the `BMesh` is still
  in scope. Pure CPU, no bgl_extended involvement; the editor's material preview drives click-to-select
  with it.
- `UiRuntime` / `UiContext` — the in-game UI: RmlUi's lifetime, its clock and log, and the file
  interface every document, stylesheet and font is read through. See *The UI runtime* below.
- `AssetManager` — constructed with an `IScene` and the project's **Data directory**. Textures,
  materials and geometry belong to the scene and are shared across every view drawn from it, so the
  manager is one per scene, and `CreateInstance` names the view each instance is placed in (holding it
  alive for as long as the instance lives there). Every asset reference Bernini stores is a path
  relative to the Data root, so it is supplied once at construction rather than threaded through every
  call.

### AssetManager: identity is the path, lifetime is a reference count

**Identity.** A path maps to one texture upload and one material, however many times it is asked for.
Geometry is keyed by `path#meshIndex`, because a `.bmesh` holds several meshes -- plus a tier suffix,
because one mesh may be live as static and as skinned (`#skinned`) geometry at once, and those are
two different uploads. Cubes and spheres have
no file, so they are not shared — but they are refcounted like anything else. Loading options
(`AssetManagerOptions`) are fixed at construction for the same reason: an option that varied per call
would make the shared material depend on who asked first.

**The containers behind that identity are cached too**, separately: the `.bmesh`, `.bskel` and
`.banim` an acquire reads, through every door. Deserializing one is most of a second on a dense rig
and a rig drawn as many meshes acquires once per mesh entry -- twenty-seven on the test project's
character -- so each is held beside the stamp it was read at. The cache never trusts itself -- the
editor authors through `assetlib`, not through this -- so a read re-stamps *and* asks
`AssetStore::GeometryIsStale`, because a re-exported source regenerates a cache entry in memory
without its bytes changing.

The blind spot it carries is **bindings**: `parametersHashOf` excludes them deliberately, so a
rebind is served from the cache until something else invalidates the entry.

**Lifetime.** References run along the edges the assets themselves have:

```
instance -> geom -> material -> texture
         \-> material (per-submesh override, when one is worn)
```

`AcquireMesh` acquires the materials its submeshes name, which acquires the textures those materials
name; `Release*` runs the chain in reverse and destroys only at zero.

That is not just tidy — **it is what makes deletion safe**. `bgl_extended` deliberately tracks nothing, and
documents preconditions it cannot check: a material may not be deleted while a submesh is bound to it,
a texture may not be deleted while a material routes it, and geometry may not be deleted while an
instance references it (`IScene::DeleteGeom`). A reference count of zero *means* exactly those things.
The manager owns instances for that last one: an instance holding a reference on its geom is what makes
"the last reference is gone" imply "nothing is drawing it".

**Swapping.** `SetSubmeshMaterial` rebinds a submesh (acquiring the new material, releasing the old).
`SetMaterialTexture` / `SetMaterialRoute` swap a map on a live material: the scene rewrites the entry
in place (`IScene::UpdatePbrMaterial`), so the handle stays valid and every submesh bound to it follows
without being rebound. The material is shared by path, so the change is seen by everything using it.

**Prefetching.** Loading a texture is two steps with opposite constraints: `assetlib::loadKTX2`
transcodes a whole Basis mip chain — expensive, and pure CPU, so it can run on any thread — and then
`IScene::AddTextureAsset` uploads it, which must be on the render thread like every other bgl call.
Fused, the expensive half is stuck on the render thread.

`TexturePrefetch` unfuses them. It is a map of already-decoded `ImageData` keyed by the relative path
it will be asked for; hand one to `AcquireTexture` / `AcquireMaterial` and a matching entry is moved
out and uploaded instead of the file being read. `MaterialTextures()` is public so a caller can see
what a material will need *before* acquiring it, and decode that list off-thread. A supplied prefetch
is authoritative: a path it does not carry resolves to the scene's default map rather than a read of
the file, so handing one in *is* the guarantee the acquiring thread does no decode. A texture whose
decode failed is simply left out, and was reported where it failed.

The editor's `AssetThumbnailCache` is the reason it exists: it decodes on a worker and uploads on the
UI thread, which is the only way a folder of meshes can populate without freezing the editor.

**Skins.** `SetSubmeshMaterial` changes a geom's **default**, so it reaches every instance placed from
it. `SetInstanceSubmeshMaterial` overrides **one instance** and leaves its siblings alone — the same
unit mesh, a different material per unit. The override outranks the default and holds a reference of
its own, which is the edge above: `ClearInstanceSubmeshMaterial` and `DestroyInstance` release it.
Without that reference `bgl_extended` would happily let the material be deleted out from under an instance
still wearing it, since a binding there is a bare slot index with no generation
(`ISceneView::SetSubmeshMaterialOverride`).

## The UI runtime

`UiRuntime` ([include/gamelib/ui/UiRuntime.h](include/gamelib/ui/UiRuntime.h)) owns RmlUi. It is
**one per process** and says so by throwing: `Rml::Initialise`, the interfaces and the context
registry are all global, so a second instance would install its own over the first's and free them
under it on the way out.

**A context is handed out as `Rml::Context&`, not wrapped.** A game registers its data models and
event listeners with RmlUi's own API — `CreateDataModel`, `data-event-click`, `ProcessMouseMove` —
because that API and its documentation are what the library was chosen for, and a facade over it
would be a second surface to keep in step. What gamelib owns instead is the three things RmlUi
cannot know: lifetime, the mount, and the clock.

So `Rml::` reaches a client through **one** gamelib header, which forward-declares the three types
it names and includes no RmlUi header. A client that drives a context includes `<RmlUi/Core.h>`
itself; the target is linked `PUBLIC` and its includes are imported as SYSTEM, so
`enable_strict_compiler`'s warnings-as-errors never compile RmlUi's headers — which is also why the
editor, which links gamelib and never touches a context, pays only the link.

**Every path is a mount key.** `UiFileInterface` reads through the `AssetStore`'s `IFileSystem`, so
`Authored/UI/menu.rml` resolves the same from a loose tree and from a `.bpak`, and nothing reaches
`std::filesystem`. `UiSystemInterface::JoinPath` resolves a document's `@import` or `src` against
the document's own key and puts it back in that form, refusing anything that leaves the data root
through `assetlib::requireInsideDataRoot` — the same body assetlib checks its own references with,
rather than a second rule that could disagree. A reference carrying a scheme (`target://preview`)
passes through untouched for the renderer to resolve. A refusal yields an empty path rather than an
exception: RmlUi calls `JoinPath` from inside its own parse, and the open that follows reports the
failure with the document and line already in hand.

**The clock is the client's.** `AdvanceTime` is what `GetElapsedTime` reports, so a headless test
steps a transition exactly instead of racing a wall clock, and a paused game pauses its UI by not
calling it. It is deliberately *not* `RenderJob::time`, which is a clip-local transport position
that scrubs and resets to zero.

**A document that lays out text needs a font face**, and faces are process-wide rather than per
context: `UiRuntime::LoadFontFace("Authored/Fonts/Lato-Regular.ttf")`. The repo's fixture tree
carries Lato under the SIL Open Font License, its licence beside it.

**A document can script itself**, off by default. `UiRuntimeOptions::scripting` installs RmlUi's
stock Lua plugin, which is one `Rml::Lua::Initialise` call: documents then get `<script>` blocks,
inline handlers, the element and event API, and data models declared from Lua tables — UI-only
logic living in the asset tree beside the markup rather than in the binary, which is the split
Scaleform drew with ActionScript. With it on, RmlUi's `body` tag builds a `LuaDocument` rather than
a plain `ElementDocument`; with it off, a `<script>` block is inert markup and an inline `onclick`
does nothing, so a game that binds everything from C++ never creates a VM. (The plugin is still
linked in — it is a static library and the `Initialise` call site is unconditional at link time —
so "no VM" is about what runs, not about binary size.)

**A scripted document is trusted code.** The plugin opens Lua's standard libraries, `io`, `os` and
`package` among them, so a `<script>` reaches the host directly: the mount confinement above bounds
which files the document *loader* resolves, and does not extend to what a script does once it runs.
That is why this is opt-in per runtime rather than on.

This is **not** an engine-wide script host, and the roadmap's scripting item still decides the
engine's language on its own evidence. `UiRuntimeOptions::luaState` is the seam that one arrives
through: pass the engine's state and RmlUi adds its bindings to it instead of keeping its own. A
state you pass is yours to close, and only after the runtime is destroyed.

**Drawing is `UiRenderer`**, which a client builds and hands to the runtime — it needs an
`IGraphics` the runtime knows nothing about, and a headless case swaps it for a stub. RmlUi's
geometry becomes `bgl::OverlayDraw`s on one `bgl::IOverlay`, submitted as a single job inside the
frame the client already has open, so the UI lands after the scene and after post-processing:

```cpp
game::UiRenderer renderer(*gfx, store);
game::UiRuntime  runtime(store, renderer.Interface());
// ...
gfx->BeginFrame(target);
gfx->Draw(job);                      // the 3D
renderer.Render(*gfx, *context);     // the UI over it
gfx->EndFrame();
```

The RmlUi half sits behind an implementation the header does not name, so a client that only draws
a UI still compiles no RmlUi header.

**Two texture sources.** A document's `src` is a mount key, decoded through `AssetStore::LoadTexture`
like any other image; `src="target://<name>"` resolves to a target registered with
`RegisterTarget`, which is how a live 3D render sits inside the UI (ADR-14). RmlUi's own generated
textures — the glyph atlas — arrive premultiplied in sRGB and are divided back to straight alpha on
upload, because a texture is sampled through an `_SRGB` view and decoding a premultiplied value
weights a half-covered glyph edge by `0.5^2.2` instead of `0.5` (ADR-12).

**A texture that fails to load draws a white box, not nothing.** RmlUi keeps the element and hands
over its null handle, which the overlay samples as opaque white — the same rule that gives an
untextured div its vertex colour. Loud rather than silent, which is the right failure for a
misspelled key, and pinned by a case.

`gamelib_tests`' `[ui]` cases drive the runtime headlessly through a no-op `RenderInterface`, and
`[ui][render]` adds a real device, a real `UiRenderer` and a golden. Also
`tests/src/ui/UiTree.h` dumps a context's element tree — tag, id, classes, computed border box — as
text, so a case asserts a whole layout in one call and a failure prints the tree rather than a
number.
