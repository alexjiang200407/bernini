# feat/outline-fx — a selection outline for the editor

The material editor's currently selected submesh gets a screen-space outline in the preview
viewport: the selected submesh's instances are drawn into an R8 mask, and the post-process pass
dilates that mask into a constant-width contour composited over the displayed image. This checks
off `Outline Shader` under FX in [ROADMAP.md](../../ROADMAP.md).

## What the survey found

**There is no selection concept anywhere.** No highlight, picking, or hover exists in `libs/` or
`apps/editor` — the material editor's selection is a plain `int m_CurrentSubmesh` (selector index)
in [MaterialEditorWindow.h](../../apps/editor/src/Windows/MaterialEditor/MaterialEditorWindow.h),
and `MaterialPreviewWindow::m_SubmeshRefs` already maps that index to `{geomIndex, localSubmesh}`
with `m_Instances` holding every placed instance —
`MaterialPreviewWindow::SetSubmeshMaterial` walks exactly the mapping an outline needs. Every bgl
call from the editor goes through `Renderer::Post`/`Invoke` onto the render thread.

**The renderer draws one submesh of one placed instance per drawable.** `SubmeshInstance`
([types/SubmeshInstance.h](../../libs/bgl/src/types/SubmeshInstance.h)) is the unit; the forward
pass expands GPU-culled, PSO-bucketed lists of them through the shared `Forward_StaticMesh`
amplification/mesh shaders. Crucially, `DrawTransparent` already demonstrates drawing an
*arbitrary index list* through those same shaders: `baseTable = kDepthSorted` makes
`ResolveInstanceBase()` return 0 and the AS reads whatever buffer is bound as
`expansionData.compactedInstances` ([forward/ExpansionData.slang](../../libs/bgl/shaders/src/forward/ExpansionData.slang)).
A selection mask draw is that same shape with a CPU-known count, so it needs no culling, no
histogram, and no new expansion logic.

**Fullscreen passes have one canonical pattern.** `TaaResolvePass` / `PostProcessPass`: a
`MeshletKernel` from one module, inputs declared as `TextureArg`s, the RTV bound through a
`FrameBuffer` in `exec`, cbuffer fields matched by Slang reflection. `PostProcess` is
**deliberately the only writer of the backbuffer** ([docs/passes.md](../passes.md)) — that keeps
`SubmitCapture` honest, so the outline must not add a second backbuffer writer.

**Stencil is a trap on D3D12.** The depth buffer is D24S8 on both backends and the
`DepthStencilState` desc is complete, but the D3D12 command list never calls `OMSetStencilRef` and
nothing reads `dynamicStencilRef` — a stencil-mask outline works on Metal today and silently runs
with ref 0 on D3D12. Any stencil design starts with an RHI change.

**TAA would fight an overlay.** Jitter is applied by `RenderContext::Draw` and carried in
`viewData`; the resolve runs before `PostProcess`. An outline drawn from a jittered mask and not
temporally accumulated shimmers by ±half a pixel; one accumulated by TAA ghosts on camera motion.
The mask must be drawn unjittered and composited after the resolve.

**Where new per-view state goes.** `ISceneView::SetSubmeshMaterialOverride` is the precedent for a
per-instance-per-submesh mutator; `SceneView::MeshMeta` holds the parallel CPU-side vectors;
`SceneView`/`Scene::AttachToFrameGraph` import per-view buffers under the `v{n}:` namespace;
`DrawData` is how per-draw parameters reach passes.

## Design decisions

**Screen-space mask + dilation, not geometry extrusion, not stencil, not blur.**
- *Inverted hull rejected:* extrusion along vertex normals breaks on hard edges and non-manifold
  meshes, its width varies with depth and FOV, and it would thread a new PSO bucket through the
  GPU cull/compact path (`c_Psos` is positionally coupled to `PsoType`).
- *Stencil rejected:* blocked on the D3D12 `OMSetStencilRef` gap above; an RHI change buys nothing
  a mask target doesn't already give, since a wide outline needs a screen-space dilate anyway.
- *Gaussian blur on a cutout rejected:* produces a soft glow whose width and opacity vary with
  silhouette shape; a selection outline wants a crisp constant-width contour. Blur also needs two
  extra fullscreen passes (H+V) against dilation's one loop.
- *Jump flooding rejected:* JFA earns its cost for outlines tens of pixels wide; for a 2–3 px
  editor outline a small fixed neighborhood sample is fewer passes and no ping-pong textures.

**Selection is per `(instance, submeshIndex)` on `ISceneView`,** mirroring
`SetSubmeshMaterialOverride` — same granularity the prompt asks for, same granularity the renderer
already draws by. The editor, which owns the selection concept, drives it; bgl only stores it.

**The mask is drawn by reusing `Forward_StaticMesh` AS/MS with a trivial pixel shader.** The
`SceneView` keeps the selected set on the CPU, resolves it each frame to packed `SubmeshInstance`
indices, and uploads a small list; the mask pass binds that list with `baseTable = kDepthSorted`
and issues a direct `DispatchMesh(count, 1, 1)` — the CPU knows the count, so nothing is indirect.
*Rejected:* widening `InstanceVisibility` with a selected bit and a second GPU compaction — that
touches four cull/compact shaders to select what the CPU already knows exactly, and ties selection
to a view's frustum cull.

**No depth test on the mask draw: the outline is the full silhouette, visible through occluders.**
Selection feedback should answer "where is the thing I selected" even when it is behind something;
this also keeps the mask pass free of any depth attachment. *Rejected:* depth-tested
(`LessOrEqual`, write off) visible-silhouette outline — worse for finding occluded selections, and
it drags the depth resource into the pass for no gain in the single-mesh preview scene. The mask
uses the **unjittered** view-projection (zero jitter in its `viewData`), so the contour is stable
under TAA without being accumulated.

**The outline composites inside `PostProcess`, after AgX, in display space.** The mask is a
`TextureHandle` + style fields on `gPostProcessData`; the pixel shader samples the mask's
neighborhood, and where the center is empty but the neighborhood is not, blends the outline color
over the tonemapped result. This keeps `PostProcess` the sole backbuffer writer, costs no extra
full-res texture, and makes the outline a UI-crisp constant color unaffected by exposure, tonemap
or TAA. *Rejected:* a separate composite pass in the HDR chain rewiring `postProcessArgs.source`
the way TAA does — an extra full-res RGBA16F target, and the outline color would be radiance,
shifted by exposure and AgX.

**The mask texture is owned by the render target,** R8_UNORM at scene-color resolution, created
and resized beside the velocity buffer. *Rejected:* lazy allocation (churn and a special case for
one cheap texture) and pass-owned textures (the pass has no resize hook; the target already has
one). At a low render scale the outline widens slightly with the upscale — accepted; width is in
render-scale pixels like every other screen-space quantity.

**Style is fixed for now** — outline color and width are bgl constants (orange, 2 px). A
per-view `SetOutlineStyle` is an obvious later extension; deliberately left out to keep the API
surface at what the feature needs. The level editor is also deliberately untouched — the prompt
scopes the trigger to the material editor, and the bgl API this adds works for any view when the
level editor grows a selection.

## What changes

- `libs/bgl/include/bgl/ISceneView.h` — `SetSubmeshSelected(instance, submeshIndex, bool)`,
  `ClearSelection()`, `IsSubmeshSelected(instance, submeshIndex)`. Invalid handle/index throws
  `SceneError`, matching the override methods.
- `libs/bgl/src/scene/SceneView.{h,cpp}` — selection flags beside `MeshMeta::overrides`; a
  per-view selected-list `ComputeBuffer<uint>` rebuilt/uploaded when the selection or instance set
  changes; imported into the graph under the view's namespace. `DeleteMeshInstance` drops the
  instance's selection with it.
- `libs/bgl/src/gfx/RenderTargetBase.*`, `d3d12/RenderTarget_d3d12.*`, `metal/RenderTarget_metal.*`
  — the R8 selection-mask texture + RTV/SRV, cleared/resized with the target.
- `libs/bgl/src/passes/SelectionMaskPass.{h,cpp}` (new) — clears the mask and draws the selected
  list; kernel = `Forward_StaticMesh` AS/MS + new `SelectionMask.slang` PS (writes 1.0, one R8
  RTV, no depth). Attached per `Draw` when the view has a selection.
- `libs/bgl/shaders/src/SelectionMask.slang` (new), `PostProcess.slang` — mask PS; outline
  neighborhood sample + composite behind an enable flag in `gPostProcessData`.
- `libs/bgl/src/gfx/RenderContext.{h,cpp}` — own/init/release the pass, attach it in `Draw` with
  the unjittered matrices, feed the mask + style into `postProcessArgs` in `EndFrame`.
- `apps/editor/src/Windows/MaterialEditor/MaterialPreviewWindow.{h,cpp}` —
  `SetSelectedSubmesh(int selectorIndex)`: on the render thread, `ClearSelection()` then
  `SetSubmeshSelected` for every instance of the ref's geom.
- `apps/editor/src/Windows/MaterialEditor/MaterialEditorWindow.cpp` — `SelectSubmesh` and the
  post-`SetPreviewGeometry` initial selection call it.
- `docs/passes.md`, `docs/bgl_api.md` — updated in the PRs that change what they describe.

**What could break:** the PostProcess cbuffer is reflection-matched by name — the shader and
`PostProcessPass` must move together. The selected-list rebuild must track `PackedBuffer`
compaction (a delete moves packed indices), so it re-resolves from slot handles rather than
caching indices. The mask draw binds a one-attachment framebuffer to a one-RTV PSO — the
count-match contract in docs/passes.md.

## Tasks

1. **The plan** — this document. *Gate: review.*
2. **bgl: per-submesh selection state on `ISceneView`** — API, CPU storage, the selected-list
   buffer upload and graph import. Dead scaffolding until task 3; the tests are its caller.
   *Gate: new `[selection]` cases in `bgl_tests` — set/clear/query semantics, `SceneError` on a
   stale handle or out-of-range submesh, selection dropped on `DeleteMeshInstance`, list survives
   an unrelated delete (packed-index re-resolve).*
3. **bgl: the mask pass and the outline composite** — mask texture on the render target,
   `SelectionMaskPass`, `SelectionMask.slang`, the `PostProcess` extension, `RenderContext`
   wiring. *Gate: a golden-image test (`SelectionOutline_test.cpp`, modeled on the TAA/PBR render
   tests) — sphere with one selected submesh shows the contour, nothing selected reproduces the
   old image; `just run bgl_tests -- --gpu-validation` clean.*
4. **editor: the material editor drives it** — `SetSelectedSubmesh` walk, called on submesh
   switch and initial population. *Gate: `editor_tests` case asserting a selector change marks
   exactly the ref's instances selected in the preview view (via `IsSubmeshSelected`); manual
   `just run editor` check on the preview.*
5. **Graduate the docs, delete the plan** — what should outlive the feature moves into
   `docs/passes.md` / `docs/bgl_api.md` (most of it lands there in tasks 2–4 already); reconcile
   anything this plan still promises that shipped differently; delete this file. *Gate: review of
   the landing diff.*
