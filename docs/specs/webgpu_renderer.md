# A second renderer, for the browser

## What it is

Bernini has one renderer *architecture*, not one backend. It is bindless end to end, mesh-shader
only, and GPU-driven. Two backends implement it — `bgl_d3d12` and `bgl_metal` — chosen at configure
time by `RENDERER_BACKEND` ([libs/bgl/CMakeLists.txt:66-70](../../libs/bgl/CMakeLists.txt)), and both
clear a hardware bar the browser does not. WebGPU has no descriptor indexing, no mesh shaders, no
multi-draw indirect and no indirect count.

**The RHI cannot carry a WebGPU backend.** It is not merely shaped around those features; it cannot
express a raster draw at all. The only graphics pipeline object is `IMeshletPipeline`, "amp/mesh/pixel"
([docs/rhi.md:147](../rhi.md)), and the command list records "compute and mesh-shading dispatch"
([docs/rhi.md:143](../rhi.md)) — the draw verbs are `Dispatch`, `DispatchMesh` and
`DispatchMeshIndirect` ([docs/rhi.md:192,286](../rhi.md)). A WebGPU backend under that interface
would be implementing pure-virtual methods it has no equivalent for.

It is worse structurally. `libs/bgl/CMakeLists.txt:7` excludes only `src/d3d12|metal` from
`bgl_objects`, so Scene, SceneView, the FrameGraph and **every pass** are backend-common — and every
geometry pass is a mesh-shader draw ([docs/passes.md:213-431](../passes.md)). A third RHI arm would
link a common half it cannot run.

**The seam that can carry it is bgl's public interface**, and it is in better shape than expected:

* Every method on `IGraphics` ([IGraphics.h:107-250](../../libs/bgl_intfc/include/bgl/IGraphics.h)),
  `IScene` ([IScene.h:189-440](../../libs/bgl_intfc/include/bgl/IScene.h)), `ISceneView` and
  `IRenderTarget` names no descriptor, meshlet or pipeline. The vocabulary is geometry, materials,
  textures, instances, cameras.
* `CreateGraphics` is a free function *defined inside the backend*
  ([libs/bgl/CMakeLists.txt:82-83](../../libs/bgl/CMakeLists.txt)), and backend choice is already a
  configure-time switch — so the swap mechanism exists.
* The cooked asset format already serves a raster renderer. `assetlib::Submesh` carries
  `vertexByteOffset` / `vertexCount` / `indexByteOffset` / `indexCount` / `indexType`
  ([Mesh.h:41-45](../../libs/assetlib_structs/include/assetlib_structs/Mesh.h)) *beside*
  `firstMeshlet` / `meshletCount` (`Mesh.h:46-47`), and `BMesh` carries `indexData` beside the three
  meshlet arrays ([BMesh.h:25-30](../../libs/assetlib_structs/include/assetlib_structs/BMesh.h)).
  A raster renderer reads the plain ranges and ignores the rest.
* `PreparedStaticMesh` is pimpl'd ([PreparedStaticMesh.h:36,44](../../libs/bgl_intfc/include/bgl/PreparedStaticMesh.h)),
  so the cooked intermediate exposes no layout.
* No GPU struct layout is public: `IDL_PUBLIC_CPP_SOURCES` holds only the `MaterialType` enum, and
  [docs/idlgen.md](../idlgen.md) states the rule that keeps it so. (It held `PsoType` too when this
  was written, which this spec wrongly read as harmless — see *What has already landed*.)
* There is **no shader entry point in the public API at all** — no path, no blob, no registry — so a
  second renderer inherits no shader obligation.

What was *not* neutral has since been fixed — see [What has already landed](#what-has-already-landed).

## The trigger

**The browser is a target.** That is the decision that makes this stop being deferrable.

Nothing about it is urgent yet — no work is scheduled and none should start. What *is* urgent is
that the interface is neutral today largely by accident, and nothing records that it must stay that
way. Two specific things will rot without a note:

1. **`assetlib::Submesh`'s plain vertex/index ranges are load-bearing and nothing says so.**
   `indexData` and `indexByteOffset`/`indexCount`/`indexType` are, as far as the shipped renderer is
   concerned, dead weight — bgl uploads `meshletTriangles` and `meshletVertices` instead. Somebody
   will eventually delete them as an obvious cook-size win. That single change is what turns this
   spec from "a lot of work" into "re-cook every asset first." `sizeof(Submesh) == 96`
   (`Mesh.h:54`) pins the layout but says nothing about why the fields exist.
2. **The public surface drifts one field at a time.** `TextureAssetHandle::shaderIndex` (`srvBindlessIndex` when this was written)
   ([TextureAssetHandle.h:13](../../libs/bgl_intfc/include/bgl/TextureAssetHandle.h)) is already there, and
   nothing stops the next one.

That cheap half has since landed (see *What has already landed*). What remains in this spec is the
renderer, and nothing about it is scheduled.

## The design

### The layer: a sibling to bgl, not a backend under the RHI

`bwgu` implements bgl's public interface and shares nothing below it. It is a second renderer, not a
second backend, and should be budgeted as one: it shares the interface, the scene vocabulary and the
assets, and essentially zero rendering code.

Rejected: **a WebGPU RHI backend.** Impossible, for the reasons in *What it is* — no raster pipeline
object, no raster draw verb, and a backend-common half that is entirely mesh-shader code.

Rejected: **adding a vertex-pipeline path to the existing renderer**, so one renderer serves both.
That doubles every pass and every shader in `bgl_objects` permanently, to serve a target that also
needs a different culling design (no multi-draw, no indirect count) and a different material binding
design (no descriptor indexing). The two architectures disagree about more than the draw call.

### One renderer per build

`BGL_API` is a single macro with no per-DLL name
([api.h:3-11](../../libs/bgl_intfc/include/bgl/api.h)) — `dllexport` under `BGL_EXPORTS`, `dllimport`
otherwise. Two renderer DLLs cannot coexist in one binary, and the public headers carry explicit
`template class BGL_API core::SharedRef<...>` instantiations that assume exactly one exporting DLL.

This is fine and should stay: you never ship a WebGPU renderer and a D3D12 one in the same binary.
`bwgu` is therefore a **third `RENDERER_BACKEND` arm** producing the same target name. The interface
is a compile-time contract, not a runtime one.

But it is not the same *kind* of arm as `METAL`, and the distinction matters when the CMake is
written. `DX12` and `METAL` select a backend *under* a shared `bgl_objects`; `WEBGPU` selects a
different renderer *instead* of it, because `bgl_objects` is the mesh-shader half. So the third arm
substitutes a `bwgu_objects` for `bgl_objects` rather than adding a directory beside
`src/d3d12`/`src/metal`. The variable name survives the change; the shape of the `if` does not.

Three consequences worth knowing before writing it:

* **`CookStaticMesh` and `PreparedStaticMesh`'s four special members are defined in
  `libs/bgl/src/scene/Scene.cpp`** — the backend-agnostic half — so today they are compiled once
  regardless of backend. A `WEBGPU` arm does not inherit them and supplies its own, which is exactly
  what the pimpl is for.
* **`PreparedStaticMesh.h:39` declares `friend class Scene;`** against a class only forward-declared
  in the `bgl` namespace. A second renderer's scene class must therefore also be `bgl::Scene` to
  construct one. That is harmless while one renderer is built per binary, but it is a real
  constraint on the implementation and nothing else records it.
* **`bgl_tests` links `bgl_objects` plus a backend's objects directly** rather than the `bgl` DLL
  (`libs/bgl/CMakeLists.txt:227-235,251-252` on DX12, `:290,309-318` on Metal), and the two variants
  are already fully duplicated in that file. A third renderer needs its own suite target on the same
  pattern; the existing ones cannot be reused.

`RENDERER_BACKEND` is set only in `CMakePresets.json:27` (`METAL`) and `:85-86` (`DX12`) — hidden
preset fragments, with no `option()` in the root `CMakeLists.txt`. Every consumer tests it by string
equality against `"DX12"` or `"METAL"`, including the executable-level Agility SDK links in
`apps/editor/CMakeLists.txt:145`, `apps/editor/tests/CMakeLists.txt:40` and the four examples. Those
are all *positive* `DX12` checks, so a third value skips them correctly and none of them needs
touching.

Rejected: **runtime backend selection** (`CreateGraphics(backend, opts)`). It buys nothing for this
target and costs the `BGL_API` model, the explicit instantiations, and a single-renderer link.

### Sharing the interface: `bgl_intfc`

**This section describes shipped code.** `libs/bgl_intfc` and `bgl_intfc_selfcheck` exist; what
follows is why they are shaped as they are, kept here because a second renderer is what they were
built for. It is *not* a dispatch mechanism, which is the thing readers assume first.

It is **not** a dispatch mechanism. Given the previous section, `bgl_intfc` does not let two
renderers coexist and does not make backend choice dynamic. Its whole value is as a **guardrail**:
a target that contains the headers and *only* their external dependencies, so the interface cannot
quietly acquire a dependency on renderer internals while nobody is building the second renderer.
That is precisely the risk during the years this spec sits unimplemented.

**The headers are hoisted, never copied.** There is exactly one copy of the public surface and both
renderers compile against it; duplicating it would defeat the entire point, since the thing being
protected is that one contract has one definition. So the interface gets its own directory and
`libs/bgl/` becomes what it actually is — the mesh-shader renderer:

```
libs/bgl_intfc/include/bgl/   the public surface. One copy. Owned by bgl_intfc.
libs/bgl/src/                 the mesh-shader renderer   -> bgl_objects, links bgl_intfc
  d3d12/  metal/              its two RHI backends
libs/bwgu/src/                the raster renderer        -> bwgu_objects, links bgl_intfc
```

The `bgl::` namespace and the `<bgl/...>` include spelling are unchanged, because the `include/bgl/`
suffix moves with the headers — so **no client source changes at all**, and the only edits are the
include directories in CMake and the `BGL_SHARED_HEADERS` glob at
`libs/bgl/CMakeLists.txt:9-12`. What duplicates between the two renderers is the *implementation* —
Scene, SceneView, the passes, the shaders — and that duplication is the cost of the second renderer,
not of the split.

Keeping the headers where they are and merely pointing an INTERFACE target at
`libs/bgl/include` would work identically at build time. It is rejected only because it leaves `bgl`
being both the name of the contract and the name of one of its two implementations, which is the
confusion this section exists to remove.

```cmake
add_library(bgl_intfc INTERFACE)
target_include_directories(bgl_intfc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bgl_intfc INTERFACE core assetlib_structs)
```

An INTERFACE target is never compiled, so on its own it enforces nothing. The enforcement is a
second, tiny target — one translation unit that includes the umbrella header and links only the
interface:

```cmake
# One TU: #include <bgl/bgl.h>
add_library(bgl_intfc_selfcheck STATIC src/selfcheck.cpp)
target_link_libraries(bgl_intfc_selfcheck PRIVATE bgl_intfc)
```

`bgl/bgl.h` reaches the whole surface transitively — `IGraphics.h` pulls in `IRenderTarget`,
`IScene`, `ISceneView`, `RenderJob` and `IGpuAssertionHandler`, and `IScene.h` pulls in the handle
headers and `PreparedStaticMesh` ([bgl.h:1-15](../../libs/bgl_intfc/include/bgl/bgl.h)) — so one TU proves
the closure. If a public header ever reaches into `libs/bgl/src`, this target fails to build, and it
fails in a PR rather than in three years.

`bgl_objects` then links `bgl_intfc` instead of declaring the include directory itself
(`libs/bgl/CMakeLists.txt:57-59`), and `gamelib` links `bgl_intfc` for the headers plus `bgl` for the
symbols — naming the contract rather than one renderer.

The closure really is just those two. `core` covers `Ref`/`SharedRef`/`slot_handle`/
`multi_slot_handle`; `assetlib_structs` covers `BMesh`/`ImageData`/`Skeleton`/`Animation`/`Bounds`
and links `core` PUBLIC itself. **glm needs no entry** — no public bgl header includes `<glm/...>`
directly, `bgl/glm.h:4` reaches it through `<core/glm.h>`, and `core` links `glm::glm` PUBLIC
(`libs/core/CMakeLists.txt:29-31`). No public header reaches into `libs/bgl/src`.

Two things to know before writing it:

* **`core` and `assetlib_structs` are STATIC libraries**, not INTERFACE ones, and `core` drags
  `spdlog::spdlog_header_only` in PUBLIC. So linking `bgl_intfc` pulls those archives into every
  consumer, including one that only wants the declarations. That is acceptable — every client links
  `core` anyway — but `bgl_intfc` is not the zero-cost header target the name suggests.
* **The self-check proves the include closure, not the link.** The public headers declare exported
  symbols a renderer must define — `CreateGraphics`
  ([IGraphics.h:263](../../libs/bgl_intfc/include/bgl/IGraphics.h)), `CookStaticMesh` and
  `PreparedStaticMesh`'s special members (`PreparedStaticMesh.h:20-29,58-59`) — and the four
  `template class BGL_API core::SharedRef<...>` lines instantiate in every TU that includes them,
  under `dllimport` when `BGL_EXPORTS` is absent. A STATIC library compiles fine and is never
  linked into an executable, so it never demands those symbols. That is the intended scope: this
  target guards what the headers *include*, and nothing else. It must stay a library, not an
  executable.

### What has already landed

The cheap half of this spec shipped as `feat/bwgpu-setup` (2026-08-29) and is **not** repeated here:
the public surface's neutrality sweep, `PsoType`'s demotion out of it, the `bgl_intfc` target and
its self-check, `assetlib::Submesh`'s plain index range recorded as load-bearing, and the doc
corrections. The layer decision behind it is an ADR now —
[renderer portability](../plans/renderer-portability.md).

Four things this spec got wrong, corrected by the survey that landed it:

* **The neutrality leak list was 4 sites; it was 13.** Beyond `srvBindlessIndex` and the three throw
  contracts, the surface also named meshlets in `ISceneView`, three more `IScene` contracts and
  `PreparedStaticMesh`, a PSO bucket in three places, and Metal by name inside `DeleteTextureAsset`'s
  hazard note.
* **`PsoType` was a public header and this spec called the surface clean.** Its enumerators are
  `kOpaque_StaticMesh_PBR`, `kAlphaTest_VatMesh_PBR` — concrete mesh-shader pipeline permutations,
  included by `IScene.h`, used by no public declaration and referenced by nothing outside
  `libs/bgl`. It was the largest break in the surface and this spec did not see it.
* **`srvBindlessIndex` had a reader.** `libs/bgl/src/uniforms/Uniforms.h`, internal to bgl — the
  claim that nothing read it was true of `gamelib` and `apps/editor` only.
* **The hoist touched six CMake sites, not two, and none would have failed the build.** The two
  backends spell the include path relatively (`../../include`), `bgl_tests` declares it twice more,
  and the IDL public output directory is hardcoded independently in
  `libs/bgl/idl/src/CMakelists.txt` and `scripts/gen_idl.py`. CMake and clang both accept a `-I`
  onto a directory that does not exist, so a missed site is silent.

### What this spec does not decide

The renderer itself. Every one of these is open and none needs answering to land the changes above:

* **Material binding.** Every resource is reached today by a bindless index written into a constant
  buffer, with a root signature carrying only CBVs and no descriptor tables
  ([docs/uniforms.md:51-56](../uniforms.md)). WebGPU has four bind groups and no descriptor indexing.
  Texture atlases, array textures, and bind-group-per-material are the candidates; the last one
  conflicts with the next bullet.
* **Culling and submission.** Today culling is GPU-driven, buckets by PSO, and writes indirect
  dispatch args consumed by `DispatchMeshIndirect` ([docs/passes.md:238-277,341-407](../passes.md)).
  WebGPU has `drawIndexedIndirect` but no multi-draw and no indirect count, so this design does not
  survive intact.
* **The FrameGraph.** It derives enhanced-barrier-shaped transitions
  ([docs/framegraph.md:77-91](../framegraph.md)); WebGPU barriers are implicit, so that layer is
  inert and the FrameGraph's value shrinks to scheduling and resource lifetime.
* **Shaders.** Slang targets WGSL, so the language is not a barrier — but the shaders are written
  against bindless primitives and amp/mesh stages and would be rewritten regardless. Note that
  Metal's runtime Slang→MSL compilation (`libs/bgl/CMakeLists.txt:125-126`) does not port to a
  browser; bwgu precompiles.
* **Feature parity.** TAA, the selection outline, VAT and skinning are all mesh-shader draws today.
  Which of them the browser target needs at all is a product question.

### One thing worth checking before any of this

**WebGPU may be the wrong target for the goal it is standing in for.** If the driver is device
coverage rather than the browser specifically, Vulkan is the cheaper and larger win: it has
descriptor indexing and `VK_EXT_mesh_shader`, so it fits *under* the existing RHI as a third backend
and reuses all of `bgl_objects`. [docs/shader_cache.md:20-22](../shader_cache.md) already anticipates
it by name.

The two are not alternatives — RHI is the right seam for Vulkan, bgl is the right seam for WebGPU —
but they cost an order of magnitude apart, and only one of them is a browser.
