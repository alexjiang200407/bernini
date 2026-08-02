# Temporal antialiasing, and the hair it exists to fix

The renderer has no antialiasing of any kind. It does have motion vectors — a correct, tested,
per-pixel screen-space velocity buffer — which is the expensive half of TAA and the half that is
already done. This branch adds the other half: a jittered projection, a history buffer, and a resolve
pass.

TAA is not the goal on its own. Self-occluding hair renders wrong today, and the fix for it —
stochastic (hashed) alpha — produces noise in a single frame and is only correct once something
integrates that noise over time. TAA is that something. The two land as one feature because
neither is worth shipping without the other: TAA alone antialiases geometry nobody complained about,
and hashed alpha alone looks worse than what it replaces.

> **The reference image was not legible to me.** The screenshot attached to the request arrived as a
> blank placeholder, so the diagnosis below is derived from the code path rather than from the
> artifact. It should be checked against the image before the hair tasks (T5–T6) are cut.

## Why hair looks wrong

A material marked `occlude` ([MaterialHandle.h](libs/bgl/include/bgl/MaterialHandle.h)) resolves to
the `kTransparentOcclude_*` PSO bucket, and the forward pass draws that bucket **twice**
([ForwardPass.cpp](libs/bgl/src/passes/ForwardPass.cpp), `DrawTransparent`): a depth-only pre-pass
that discards below the material's alpha cutoff and writes depth, then a blend draw with
`depthFunc == Equal`.

That pair renders **exactly one layer of hair**. The pre-pass records the depth of the nearest
fragment whose base-colour alpha clears the cutoff; the `Equal` test then rejects every strand behind
it. Three symptoms follow, and none of them is fixed by dithering the pre-pass alone:

* **No volume.** Strands behind the front layer contribute nothing, so the hair reads as a single
  shell rather than a mass. Where the front layer's alpha is low, the shell is faint and the
  background shows through at full strength, because there is no second layer under it.
* **A binary silhouette.** The cutoff is a hard test, so strand edges are jagged and crawl.
* **Per-pixel instability.** Which strand wins the depth test is decided by a hard comparison, so a
  sub-pixel camera movement flips the winner and the pixel jumps between two shaded surfaces.

Hashed alpha testing (Wyman & McGuire, *Hashed Alpha Testing*, I3D 2017) replaces the fixed cutoff
with a per-pixel threshold hashed from the surface's own coordinates. Alpha stops being a binary test
and becomes **stochastic coverage**: each pixel keeps one strand, chosen with probability equal to its
alpha, and the correct blend is what the ensemble of pixels averages to. Every kept fragment writes
real depth, so the surface is ordinary opaque geometry — no sort, no pre-pass, no `Equal` trick, and
every layer participates.

The ensemble is the catch. One frame of it is visibly noisy. The noise is resolved by averaging over
pixels (which a screen-space hash gives for free) and over frames (which is TAA). This is why the
order below is TAA first.

## What the survey found

**Colour goes straight to the swapchain.** `RenderContext::BeginFrame` imports
`rt.GetBackbufferTexture(index)` as `c_BackbufferName` and every colour-writing pass takes it as its
render target ([RenderContext.cpp:348](libs/bgl/src/gfx/RenderContext.cpp)). There is no offscreen
scene-colour texture and no post-processing stage of any kind. `PreparePresentPass` transitions the
backbuffer to present and does nothing else.

**Colour is already tonemapped when it is written.** `PbrShading.slang` imports `util.Tonemap`, so
the value the forward pass writes is LDR, and the PSO's sole colour format is hardcoded
`Format::SBGRA8_UNORM` in `BuildForwardKernel` — an sRGB render-target view, so the encode/decode is
the hardware's.

**Motion vectors are done and correct.** `RG16_FLOAT`, owned by the render target with
`kRenderTarget | kSRV` and already meant to be resampled
([RenderTarget_d3d12.cpp:183](libs/bgl/src/d3d12/RenderTarget_d3d12.cpp)); cleared to zero each frame;
written as MRT slot 1 by every non-blend PSO from `ComputeMotionVector`
([forward/common.slang](libs/bgl/shaders/src/forward/common.slang)).
`SceneView::AdvanceCamera` holds the previous frame's `ViewMatrices` per view and is already correct
for a view drawn twice in one frame. The transparent phase writes no velocity.

**There is no jitter anywhere.** `RenderContext::Draw` takes `job.camera.GetViewProjection()`
verbatim. `Camera` ([Camera.h](libs/bgl/include/bgl/Camera.h)) is a public value type with no renderer
hook.

**The depth buffer is not sampleable.** `D24S8` created with `TextureUsageFlag::kDepthStencil` alone
([RenderTarget_d3d12.cpp:158](libs/bgl/src/d3d12/RenderTarget_d3d12.cpp)). Reading depth in a pass
means adding `kSRV`, picking the typeless resource / `R24_UNORM_X8` view format split, and doing it in
both backends.

**There are no UAV textures.** `TextureUsageFlag` is `{ kSRV, kDepthStencil, kRenderTarget }`
([resource/Texture.h](libs/bgl/src/resource/Texture.h)) and the resource manager creates SRV, RTV and
DSV descriptors only. Compute cannot write a texture today.

**A full-screen render-to-texture pass has a precedent.** `BrdfLutGenPass`
([passes/BrdfLutGenPass.cpp](libs/bgl/src/passes/BrdfLutGenPass.cpp)) builds a mesh+pixel
`MeshletKernel` with depth test off and issues `DispatchMesh(1, 1, 1)`;
[FullscreenRect.slang](libs/bgl/shaders/src/FullscreenRect.slang) already emits the covering triangle
with UVs. Sampling a `TextureHandle` from a pixel shader is an assignment to a uniform
([Uniforms.h:231](libs/bgl/src/uniforms/Uniforms.h)), as `SkyboxPass` does for the cube.

**RTV descriptors are budgeted.** `GraphicsOptions::maxRtvs` defaults to 8
([IGraphics.h](libs/bgl/include/bgl/IGraphics.h)) and one render target already spends three (two
backbuffers, one velocity buffer). The editor creates a target per viewport window plus one for the
thumbnail cache.

**Single-frame readback is how the renderer is tested.** `bgl_tests` draws one or a few frames and
asserts on the pixels (`MotionVectors_test.cpp`, `PbrRender_test.cpp`, `AlphaTest_test.cpp`,
`Transparent_test.cpp`). `AssetThumbnailCache` renders a thumbnail the same way. Anything that makes
frame *N* depend on frames before it changes what all of them see.

**`LayerType` is the authoring axis that picks the bucket.** `{ kOpaque, kMask, kBlend }`
([LayerType.h](libs/bgl/include/bgl/LayerType.h)); `SceneView::ResolveShading` maps the
`(LayerType, MaterialType)` pair through `SubmeshPso` to a `PsoType`, and `occlude` is a bool on top
of `kBlend`. The same field is carried by `.bmaterial`
([BMaterial.h](libs/assetlib_structs/include/assetlib_structs/BMaterial.h)) and authored in the
material editor's blend output node.

## Design decisions

### The forward pass renders to an offscreen `sceneColor`, and only the resolve writes the backbuffer

TAA reads this frame's colour and blends it against a history that must survive the frame. A
flip-model swapchain image is neither: its contents after `Present` are undefined and there are only
`c_SwapchainImageCount` (2) of them. So when TAA is on, `Clear`, `Skybox` and `Forward` take a
target-owned `sceneColor` texture instead, and the resolve pass becomes the only writer of the
backbuffer.

*Rejected:* leaving forward output in the backbuffer and adding history alone — the resolve would
have to sample the surface it is writing.

`sceneColor` keeps `SBGRA8_UNORM`, so `c_Psos`' single hardcoded RTV format, the MRT velocity shape,
and the blend/pre-pass framebuffer shapes are all unchanged. It is an sRGB view, so a sample decodes
back to the linear tonemapped value and history is accumulated in that space — which is the space TAA
wants for its neighbourhood weighting anyway. *Rejected:* an HDR `R11G11B10_FLOAT` scene colour with a
separate tonemap pass. It is the better architecture and it is a different change, with its own reason
to happen; folding it in here doubles the blast radius and puts a tonemapping argument inside a TAA
review.

### The resolve is a raster full-screen pass, not compute

The RHI has no UAV texture. A compute resolve means a new `TextureUsageFlag`, a UAV descriptor path,
and `BarrierLayout::kUnorderedAccess` plumbing through both backends before one pixel of TAA exists.
`BrdfLutGenPass` already shows the raster shape and `FullscreenRect.slang` already has the triangle.

*Rejected:* compute. Revisit when something needs UAV textures for its own sake.

### One MRT draw writes the backbuffer and the new history, off a two-texture ping-pong

The resolved colour is the value both destinations want, so the resolve declares two RTVs and writes
it twice from the same registers. History is two textures because the pass samples last frame's while
writing this frame's, and a resource cannot be an SRV and an RTV in one pass.

*Rejected:* resolve into history and then blit to the backbuffer — a second full-screen pass for a
value already computed. *Rejected:* a single history texture — illegal.

### Jitter is applied inside `RenderContext::Draw`, and subtracted out of motion vectors as an offset

TAA is a renderer concern. If jitter lived in `Camera`, every caller — editor, thumbnail cache, tests,
five examples — would have to opt in by hand, and any caller that reads `GetViewProjection()` back for
picking would silently get a jittered matrix. So `Draw` post-multiplies the NDC translation onto the
projection it builds, and the public `Camera` never sees it.

`ForwardVSOut::clip` and `prevClip` are then both jittered. Velocity must describe the surface, not
the sample pattern, so `ViewData` carries this frame's and last frame's offsets as two `float2`s and
`ComputeMotionVector` subtracts them before differencing.

*Rejected:* carrying an unjittered `viewProj`/`prevViewProj` pair in `ViewData` — 128 bytes and two
more matrix multiplies per vertex to say what 16 bytes says. *Rejected:* jittering only the viewport
offset — it cannot express a sub-pixel shift.

The sequence is Halton(2, 3), 8 samples, scaled to ±0.5 pixel from the viewport dimensions. Eight
rather than sixteen because the history blend is what fills in the rest of the distribution, and a
longer sequence lengthens the ghosting tail for no visible gain at this blend weight.

### TAA is opt-in per render target, and off by default

Every render test draws a fixed small number of frames and asserts on the readback. Jitter alone moves
all of them; an unconverged history moves them again. The thumbnail cache is the same shape and wants
a converged image from few frames, which TAA cannot give it. So `RenderTargetDesc` gains
`bool temporalAA = false`; the extra textures and RTVs are allocated only when it is set, and a frame
with it off is byte-identical to today's — no `sceneColor`, no blit, no jitter.

*Rejected:* a `GraphicsOptions` flag. The editor drives its viewport and its thumbnail cache from one
`IGraphics` and wants opposite answers.

This is also what keeps the RTV budget honest: a TAA target spends six of the default eight, so
turning it on everywhere by default would exhaust the heap on the editor's second viewport.

### The first resolve clamps to the 3×3 neighbourhood and reads no depth

Sampling depth means giving the depth buffer `kSRV`, resolving the typeless/`R24_UNORM_X8` view split,
and doing it in both backends — before any of it can be evaluated. The YCoCg neighbourhood clamp is
what removes the bulk of ghosting; closest-fragment velocity dilation and depth-based disocclusion
rejection are refinements on top of a working resolve, and land as their own task if the images ask
for them.

### Hashed alpha is a new `LayerType`, not a redefinition of `occlude`

`LayerType` gains `kHashed`, with its own PSO pair in the **opaque** shape — depth write, no blend,
velocity MRT — and no pre-pass. Existing `occlude` materials are untouched.

The obvious alternative is to redefine `occlude` to mean hashed alpha and delete the pre-pass path
entirely; it is very likely where this ends up, and it would delete two PSO rows, two prepass kernels,
`Forward_Transparent_Prepass`, and the transparent sort's `[occlude][plain]` partition. It is
*rejected for now* for two reasons. TAA defaults off, so redefining `occlude` would make every
existing hair asset render as raw noise for any caller that has not opted in — including the thumbnail
cache, which cannot opt in. And a separate layer type is the only way to put the old and the new
side by side in one scene, which is how the fix gets judged against the artifact it is meant to fix.
Retiring `occlude` is a follow-up, once the images say so.

The hash is taken from world position with screen-space derivatives (Wyman & McGuire §3.2), not object
position: `ForwardVSOut` already interpolates `worldPos`, and adding an object-space channel costs a
vertex attribute for a difference that only shows on a non-uniformly scaled instance. The hash seed
advances with the jitter index so the noise is decorrelated frame to frame — without that, TAA
converges to the noise instead of through it.

## What changes

| | |
|---|---|
| `libs/bgl/include/bgl/IRenderTarget.h` | `RenderTargetDesc::temporalAA` |
| `libs/bgl/include/bgl/LayerType.h` | `kHashed` |
| `libs/bgl/src/gfx/RenderTargetBase.h` | scene-colour and history accessors, `HasTemporalAA()` |
| `libs/bgl/src/d3d12/RenderTarget_d3d12.{h,cpp}` | allocate/resize/release the TAA texture set |
| `libs/bgl/src/metal/RenderTarget_metal.{h,mm}` | the same, second backend |
| `libs/bgl/src/gfx/RenderContext.cpp` | import + clear `sceneColor`, route `DrawData`, build the jitter, attach the resolve |
| `libs/bgl/src/passes/TaaResolvePass.{h,cpp}` | new |
| `libs/bgl/src/passes/DrawData.h` | `sceneColorHandle`, `jitter`, `prevJitter` |
| `libs/bgl/src/passes/ForwardPass.cpp` | two `c_Psos` rows; bind the jitter uniforms |
| `libs/bgl/src/scene/SceneView.{h,cpp}` | `ViewMatrices` carries the jitter offset; `kHashed` in `SubmeshPso` |
| `libs/bgl/src/constants/constants.h` | `c_SceneColorName`, `c_HistoryName` |
| `libs/bgl/shaders/src/TaaResolve.slang` | new |
| `libs/bgl/shaders/src/util/HashedAlpha.slang` | new |
| `libs/bgl/shaders/src/Forward_PBR_HashedAlpha.slang`, `_Loose_` | new |
| `libs/bgl/shaders/src/forward/{ViewData,common,MaterialData}.slang` | jitter fields, de-jittered velocity, the hashed discard |
| `libs/bgl/idl/src/PsoType.slang` | two rows |
| `libs/assetlib_structs/.../BMaterial.h`, `libs/assetlib` | `kHashed` through the cook |
| `apps/editor` | TAA on the viewport, off for thumbnails; the layer type in the material editor |
| `docs/passes.md`, `docs/bgl_api.md`, `docs/taa.md` | the catalog entry, the target flag, the subsystem page |

**What could break.** The RTV budget is the sharpest edge: six per TAA target against a default of
eight. Second is the resize path — the TAA textures are screen-sized and `ResizeBackbuffers` must drop
the history rather than resize into it, or the first frame after a resize resolves against garbage.
Third is `ScreenshotPng` / `SubmitCapture`, which read `GetBackbufferTexture(GetLastPresentedIndex())`
in `kPresent`; that stays true only because the resolve writes the backbuffer last. Fourth, every
existing golden-image test must be shown unchanged, which is what the default-off decision buys.

## Tasks

Bottom-up: `bgl` before `assetlib` before the editor, and every task builds and passes on its own.

### T1 — The render target grows an optional TAA resource set; forward draws offscreen

`RenderTargetDesc::temporalAA`, and when it is set: a `sceneColor` texture + RTV on the target
(both backends), `RenderTargetBase` accessors, `c_SceneColorName`, `RenderContext` clearing and
routing `DrawData` to it, and a new full-screen pass that samples `sceneColor` and writes the
backbuffer. No jitter, no history, no temporal anything — this task only moves where the pixels land.

*Gate:* a `bgl_tests` case that renders the same scene into a TAA target and a non-TAA target and
compares the two readbacks pixel-for-pixel. They must be identical: the blit is a copy.

### T2 — Jitter, and motion vectors that survive it

Halton(2, 3) on the projection in `RenderContext::Draw` when the target has TAA on; `jitter` and
`prevJitter` on `ViewData` and `DrawData`; the offset carried across frames on `ViewMatrices` beside
the matrices `AdvanceCamera` already holds; `ComputeMotionVector` subtracts both before differencing.

*Gate:* extend `MotionVectors_test.cpp`. With jitter on, a static camera over a static surface must
still report zero velocity to within the `RG16_FLOAT` floor, and a translating camera must report the
same velocity it reports with jitter off. This is the assertion that catches a sign error or a missed
subtraction, and nothing else will.

### T3 — The resolve

The history ping-pong (two textures + RTVs, allocated with the rest of the set), `TaaResolvePass`
sampling `sceneColor`, the previous history and `motionVectors`, with a 3×3 YCoCg neighbourhood clamp,
an exponential blend, and a history-invalid path that takes `sceneColor` whole. Replaces T1's blit.
`docs/taa.md` and the `docs/passes.md` catalog entry land here, because this is the frame TAA first
exists in.

*Gate:* a headless test that renders a static scene with TAA on for N frames and converges to within
a tight epsilon of the same scene rendered with TAA off — a static scene under jitter must resolve to
what it supersamples to. Plus a pan case asserting the moving edge does not smear beyond a pixel of
the unjittered result. `just run bgl_tests -- --gpu-validation` for the new descriptors and barriers.

### T4 — The editor turns it on where it should be on

TAA for the viewport windows, off for `AssetThumbnailCache`, and a toggle so the two can be compared
without a rebuild. Whatever the frame loop needs so a still camera keeps producing frames, since a
history that stops updating stops converging.

*Gate:* `editor_tests`, plus a screenshot of a static scene held for a second showing edges resolved
and no crawl.

### T5 — Hashed alpha: the shader and the PSO pair

`util/HashedAlpha.slang` (the three-level world-space hash with screen-space derivatives, seeded off
the jitter index), `Forward_PBR_HashedAlpha` and its loose twin, and two `PsoType` rows in the opaque
shape. Nothing selects them yet — this is dead scaffolding, and the PR says so.

*Gate:* a `bgl_tests` case driving the new PSO directly over a known alpha ramp. At alpha *a*, the
fraction of surviving fragments must be *a* within the sampling error, and it must differ frame to
frame — that is the whole contract, and both halves matter.

### T6 — `LayerType::kHashed` through the authoring chain

The enum, `SubmeshPso`'s mapping, `.bmaterial` and the assetlib cook, and the material editor's output
node so a hair material can be switched to it.

*Gate:* `assetlib_tests` on the round-trip, `gamelib_tests` on the resolved `MaterialHandle`, and a
render test asserting an instance authored `kHashed` lands in the new bucket.

### T7 — The hair, judged

Author the hair material as `kHashed`, capture it against the `occlude` original, and correct this
plan against what the images actually show. If the neighbourhood clamp is eating the strand highlights
or disocclusion is smearing, closest-fragment dilation and a depth SRV become a task here rather than
an assumption in T3.

*Gate:* the before/after pair in the PR body, at rest and under camera motion.
