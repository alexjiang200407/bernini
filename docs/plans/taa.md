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

> **The reference image was checked against this on 2026-08-03.** The diagnosis below was written
> from the code path — the screenshot originally arrived as a blank placeholder — and the image
> confirms it, with one addition recorded under *What the image added*.

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

### What the image added

The artifact is **card-shaped**, which the code alone did not say. The background shows through the
hair in angular patches whose edges are hair-card quads, not strands, and the face reads straight
through the hairline.

That follows from the same mechanism, one step further than the reasoning above reached: where *no*
card over a pixel clears the cutoff, the pre-pass writes no depth there at all, so the `Equal` colour
draw matches nothing and **the environment is what shows**. The surface is not merely thin — it is
holed, in the shape of the geometry that failed the test. It is the strongest evidence for the fix,
because stochastic coverage has no threshold for a card to fail.

### Ghosting under fast camera motion — reported with the same image

Two causes, and they are separable.

**The transparent phase writes no velocity.** `ForwardPass::DrawTransparent` binds a framebuffer with
only the scene-colour attachment, so every blend surface — `occlude` included — has a motion vector
of zero. Hair is `occlude`, so the resolve reprojects it as though it were static while the camera
moves. The hair is the worst-ghosting surface in the frame *by construction*, and no amount of
tuning the clamp addresses it.

**T5–T6 fix that one for free**, which is the reason they come first: `kHashed` puts hair in the
opaque bucket, which writes velocity like everything else. Re-measure after T6 before touching
anything else, because the artifact changes shape.

What is left after that is generic ghosting, from two known gaps: the clamp is a min/max box rather
than variance clipping (cheap to change, no new resources), and there is no closest-fragment velocity
dilation (which needs the depth SRV T3 deferred). That becomes **T8**, after T7's measurement says
how much of it survives.

## What the survey found

**Colour goes straight to the swapchain.** `RenderContext::BeginFrame` imports
`rt.GetBackbufferTexture(index)` as `c_BackbufferName` and every colour-writing pass takes it as its
render target ([RenderContext.cpp:348](libs/bgl/src/gfx/RenderContext.cpp)). There is no offscreen
scene-colour texture and no post-processing stage of any kind. `PreparePresentPass` transitions the
backbuffer to present and does nothing else.

**Tonemapping happens inside the shading, in three places.** `PbrShading.slang` and `Skybox.slang`
both import `util.Tonemap`, so the value every colour pass writes is already LDR, and the PSO's sole
colour format is hardcoded `Format::SBGRA8_UNORM` in `BuildForwardKernel` — an sRGB render-target
view. Transparent blending therefore happens in tonemapped space.

**Motion vectors are done and correct — but nothing samples them yet.** `RG16_FLOAT`, owned by the
render target, cleared to zero each frame, written as MRT slot 1 by every non-blend PSO from
`ComputeMotionVector` ([forward/common.slang](libs/bgl/shaders/src/forward/common.slang)).
`SceneView::AdvanceCamera` holds the previous frame's `ViewMatrices` per view and is already correct
for a view drawn twice in one frame. The transparent phase writes no velocity. The texture has an
RTV and **no SRV** ([RenderTarget_d3d12.cpp:183](libs/bgl/src/d3d12/RenderTarget_d3d12.cpp)) — it is
declared `kSRV`-capable against a future reader, and TAA is that reader, so creating the SRV is part
of this work.

**There is no jitter anywhere.** `RenderContext::Draw` takes `job.camera.GetViewProjection()`
verbatim. `Camera` ([Camera.h](libs/bgl/include/bgl/Camera.h)) is a public value type with no renderer
hook.

**The depth buffer is not sampleable.** `D24S8`, allocated depth-stencil-only and given a DSV and
nothing else ([RenderTarget_d3d12.cpp:158](libs/bgl/src/d3d12/RenderTarget_d3d12.cpp)). Reading depth
in a pass means an SRV over a depth format — the typeless resource / `R24_UNORM_X8` view split — in
both backends.

**Views are explicit, and there is no UAV view.** Since #247, a texture on its own has no descriptor:
`CreateSrv(TextureHandle, SrvDesc) -> SrvHandle` is a separate call beside `CreateRtv` / `CreateDsv`
([resource/ResourceManager.h](libs/bgl/src/resource/ResourceManager.h)), `Uniforms::operator=` binds an
`SrvHandle` rather than a `TextureHandle`, and destroying a texture does not destroy its views.
`TextureUsage` survives as a *description* of what the allocation must support and no longer selects
anything ([docs/rhi.md](docs/rhi.md)), so it is not the thing to reason about here. What matters is
that **there is no UAV view type at all** — compute cannot write a texture today, in either backend.

So every TAA texture is a create-plus-two-views: `CreateTexture`, `CreateRtv` to write it,
`CreateSrv` to read it, and both destroyed explicitly. The same applies to the existing velocity
buffer, which has only the first of the two.

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

### `sceneColor` is HDR, and the tonemap moves out of the shading into the pass that writes the backbuffer

Creating the offscreen target *is* the moment the tonemap's position gets decided, so deciding it
later means changing the format, the PSO table, the history format and the resolve all over again.
`sceneColor` is therefore `RGBA16_FLOAT` holding linear HDR, and `util.Tonemap` leaves
`PbrShading.slang` and `Skybox.slang` for the full-screen pass that writes the backbuffer — one place
instead of three.

*Rejected:* keeping the in-shader tonemap and making `sceneColor` an LDR `SBGRA8_UNORM` copy of
today's output. It leaves no HDR stage for bloom, DOF or exposure adaptation to ever live in, and it
makes TAA accumulate history in a space that is already display-encoded.

*Rejected:* `R11G11B10_FLOAT`, which is half the bandwidth. It has no alpha channel, and the blend
state writes destination alpha (`One` / `InvSrcAlpha`) which the capture path reads back into the
golden PNGs.

**This changes transparent blending**, which currently composites in tonemapped space and afterwards
composites in linear HDR. That is the more correct behaviour and it is a visible difference, not a
rounding one: the blend-carrying goldens (`alpha_test_*`, the transparent set) are rebaselined in T1
with the before/after pair in the PR body. It is the only deliberate image change on this branch.

### The resolve is a raster full-screen pass, not compute

The RHI has no UAV view type. A compute resolve means inventing one, plus its descriptor path and
`BarrierLayout::kUnorderedAccess` plumbing through both backends, before one pixel of TAA exists.
`BrdfLutGenPass` already shows the raster shape and `FullscreenRect.slang` already has the triangle.

*Rejected:* compute. Revisit when something needs UAV textures for its own sake.

### The resolve writes history only, and stays a separate stage from post-processing

The resolve reads `sceneColor`, the previous history and `motionVectors`, and writes one thing: the
new history, in `RGBA16_FLOAT` linear HDR. `PostProcess` then reads that instead of `sceneColor` and
writes the backbuffer as before. Two full-screen passes, in the order
`sceneColor → resolve → history → post-process → backbuffer`.

*Rejected — and this reverses an earlier draft of this plan:* one MRT draw writing the tonemapped
backbuffer and the new history together, absorbing the post-process pass. It saves a full-screen
pass, and that is the whole of its case. Against it: `PostProcess` is the stage where bloom, grading
and exposure adaptation land, and merging it into the resolve means every one of those arrives as a
change to the TAA shader. It also fixes the order — anything that must run *between* resolve and
display would have nowhere to go. One extra full-screen pass is the cheaper side of that trade, and
the separation is what the pass is named for.

History is two textures because the pass samples last frame's while writing this frame's, and a
resource cannot be an SRV and an RTV in one pass. It is HDR for the same reason `sceneColor` is: the
display curve is the last thing that happens, not something baked into the accumulator. The resolve
still *weights* its neighbourhood samples in a tonemapped space — that is what keeps a single firefly
from dominating the average — but what it stores is linear.

*Rejected:* a single history texture — illegal. *Rejected:* LDR history, which would put the display
curve inside the accumulation and undo the previous decision.

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
`bool taaEnabled = false`, and the history pair, the jitter and the temporal half of the resolve exist
only when it is set.

`sceneColor` and the post-process pass are **not** gated on it — they are the pipeline now, for every
target. So a non-TAA frame is not byte-identical to today's: it differs by the tonemap move, once, in
T1. After that, whether TAA is on changes nothing about a target that did not ask for it.

*Rejected:* a `GraphicsOptions` flag. The editor drives its viewport and its thumbnail cache from one
`IGraphics` and wants opposite answers.

**The RTV budget does not survive this unchanged.** `GraphicsOptions::maxRtvs` defaults to 8
([IGraphics.h](libs/bgl/include/bgl/IGraphics.h)); a target spends three today (two backbuffers, one
velocity), four after T1, and six with TAA on. Two editor viewport windows plus the thumbnail cache
already exceeds eight at four apiece. T1 raises the default and says what the new number is derived
from; the editor reads `maxRtvs` from its settings file
([MainWindow.cpp:59](apps/editor/src/MainWindow.cpp)), so a pinned stale value there is a real failure
mode to check rather than assume.

### The first resolve clamps to the 3×3 neighbourhood and reads no depth

Sampling depth means an SRV over a depth format, the typeless/`R24_UNORM_X8` view split resolved in
both backends — before any of it can be evaluated. The YCoCg neighbourhood clamp is
what removes the bulk of ghosting; closest-fragment velocity dilation and depth-based disocclusion
rejection are refinements on top of a working resolve, and land as their own task if the images ask
for them.

### Hashed alpha is a new `LayerType`, not a redefinition of `occlude`

`LayerType` gains `kHashed`, with its own PSO pair in the **opaque** shape — depth write, no blend,
velocity MRT — and no pre-pass. Existing `occlude` materials are untouched.

The obvious alternative is to redefine `occlude` to mean hashed alpha and delete the pre-pass path
entirely. It is where this ends up — see T9 — but not in the task that introduces the replacement,
because **T7 needs both paths in one scene**: the claim this branch is judged on is hashed-versus-
`occlude` on the real asset, and deleting the old path first turns that into a comparison against a
screenshot taken earlier. It is also the fallback if T7 says hashed alpha loses.

*One reason given here was wrong and is corrected.* The plan argued that TAA defaults off, so
redefining `occlude` would make hair render as noise for callers that cannot opt in — the thumbnail
cache among them. The thumbnail cache renders `c_WarmupFrames = 1` frame and captures; it is a
cached one-shot, so raising that count and enabling TAA converges it. Nothing structural stands in
the way, and the retirement is gated on evidence rather than on that.

The hash is taken from world position with screen-space derivatives (Wyman & McGuire §3.2), not object
position: `ForwardVSOut` already interpolates `worldPos`, and adding an object-space channel costs a
vertex attribute for a difference that only shows on a non-uniformly scaled instance. The hash seed
advances with the jitter index so the noise is decorrelated frame to frame — without that, TAA
converges to the noise instead of through it.

## What changes

| | |
|---|---|
| `libs/bgl/include/bgl/IRenderTarget.h` | `RenderTargetDesc::taaEnabled` |
| `libs/bgl/include/bgl/LayerType.h` | `kHashed` |
| `libs/bgl/include/bgl/IGraphics.h` | a `maxRtvs` default that covers the new per-target cost |
| `libs/bgl/src/gfx/RenderTargetBase.h` | scene-colour and history accessors, `HasTemporalAA()` |
| `libs/bgl/src/d3d12/RenderTarget_d3d12.{h,cpp}` | allocate/resize/release the texture set, and the velocity buffer's missing SRV |
| `libs/bgl/src/metal/RenderTarget_metal.{h,mm}` | the same, second backend |
| `libs/bgl/src/gfx/RenderContext.cpp` | import + clear `sceneColor`, route `DrawData`, build the jitter, attach the resolve |
| `libs/bgl/src/passes/PostProcessPass.{h,cpp}`, `TaaResolvePass.{h,cpp}` | new |
| `libs/bgl/src/passes/DrawData.h` | `sceneColorHandle`, `jitter`, `prevJitter` |
| `libs/bgl/src/passes/ForwardPass.cpp` | `RGBA16_FLOAT` colour format; two `c_Psos` rows; bind the jitter uniforms |
| `libs/bgl/src/scene/SceneView.{h,cpp}` | `ViewMatrices` carries the jitter offset; `kHashed` in `SubmeshPso` |
| `libs/bgl/src/constants/constants.h` | `c_SceneColorName`, `c_HistoryName` |
| `libs/bgl/shaders/src/{PostProcess,TaaResolve}.slang` | new |
| `libs/bgl/shaders/src/forward/PbrShading.slang`, `Skybox.slang` | the tonemap comes out |
| `libs/bgl/shaders/src/util/HashedAlpha.slang` | new |
| `libs/bgl/shaders/src/Forward_PBR_HashedAlpha.slang`, `_Loose_` | new |
| `libs/bgl/shaders/src/forward/{ViewData,common,MaterialData}.slang` | jitter fields, de-jittered velocity, the hashed discard |
| `libs/bgl/idl/src/PsoType.slang` | two rows |
| `libs/assetlib_structs/.../BMaterial.h`, `libs/assetlib` | `kHashed` through the cook |
| `apps/editor` | TAA on the viewport, off for thumbnails; the layer type in the material editor |
| `docs/passes.md`, `docs/bgl_api.md`, `docs/taa.md` | the catalog entry, the target flag, the subsystem page |

**What could break.** The RTV budget is the sharpest edge: six per TAA target against a default of
eight, with the editor pinning its own value in settings. Second is the resize path — the TAA textures
are screen-sized and `ResizeBackbuffers` must drop the history rather than resize into it, or the
first frame after a resize resolves against garbage. Third is `ScreenshotPng` / `SubmitCapture`, which
read `GetBackbufferTexture(GetLastPresentedIndex())` in `kPresent`; that stays true only because the
resolve writes the backbuffer last. Fourth, views are explicit now, so every texture added here owes
a matching `DestroySrv` / `DestroyRtv` — a missed one is a leak the live-object report catches, and a
double-free is not.

## How this is tested

There is a golden-image harness — `MatchesGolden` compares a captured PNG against
`assets/golden/*.exp.png` by mean-squared error, leaving a `.got.png` behind on failure
([tests/src/util/GoldenImage.h](libs/bgl/tests/src/util/GoldenImage.h)) — and 15 goldens using it. It
is the right instrument for "does this scene still look like itself" and the wrong one for everything
TAA actually claims, so the branch uses three tools and is explicit about which:

* **Goldens, for the images that must not drift.** The existing 15 are the regression net for T1's
  tonemap move; the blend-carrying ones are rebaselined there and everything else must pass untouched.
  A TAA golden is captured *after* a fixed convergence run, so the fixture gains "drive N frames, then
  capture" — a golden of frame 1 of a temporal algorithm asserts nothing.
* **`AliasEnergy`, for the claims that are about frequency rather than colour.** It measures mean
  squared difference between horizontally adjacent pixels, which is precisely what TAA removes and
  what hashed alpha adds before TAA removes it. So "TAA antialiases" is `AliasEnergy` on a
  high-contrast edge dropping between the TAA-off and converged-TAA capture of the same scene, and
  "hashed alpha converges" is the same measure falling across frames. Neither needs a stored
  reference, so neither goes stale when the scene changes.
* **`MeanColor` and direct readback, for the claims that are arithmetic.** Convergence to the
  unjittered image, the velocity assertions in `MotionVectors_test.cpp` (which reads the velocity
  buffer back as floats rather than comparing pictures), and the survival-fraction test for hashed
  alpha are all numbers with a right answer. They are the gates that fail loudly and locally; a
  golden that moves tells you something changed but not what.

`just run bgl_tests -- --gpu-validation` on T1, T3 and T5 — each adds descriptors, barriers or
both.

## Tasks

Bottom-up: `bgl` before `assetlib` before the editor, and every task builds and passes on its own.

### T1 — HDR `sceneColor`, and a post-process pass that writes the backbuffer

An `RGBA16_FLOAT` `sceneColor` texture with its RTV and SRV on every render target (both backends),
`RenderTargetBase` accessors, `c_SceneColorName`, `RenderContext` clearing and routing `DrawData` to
it, `RGBA16_FLOAT` as `BuildForwardKernel`'s colour format, `util.Tonemap` out of `PbrShading.slang`
and `Skybox.slang` and into a new `PostProcessPass`, and a `maxRtvs` default that covers four per target.
No TAA at all — this task decides where the pixels land and where the display curve is applied.

*Gate:* the 15 existing goldens, with the ones that move rebaselined and justified per image. Plus
`just run bgl_tests -- --gpu-validation` for the new SRV and the extra pass's barriers.

**This plan predicted the wrong set, in both directions.** It said the blend-carrying goldens would
move and the rest would not. What actually moved was `cube`, `plane`, `plane_floor`, `sphere_cube`
and `two_cubes` — every one of them drawn by `kOpaque_StaticMesh_Null`, whose pixel shader writes a
literal `float4(1, 1, 1, 1)`. That constant is radiance now, so the display curve maps it to about
0.8 instead of leaving it white. The blend goldens did not move: the transparent tests compare two
captures against each other rather than against a stored image, and the `alpha_test_*` set is
alpha-*tested* PBR shading, where moving the curve later is arithmetically neutral. So the real
lesson is that self-referential assertions survive a pipeline change and stored references do not,
which is the argument for the `AliasEnergy`/`MeanColor` gates in the sections above.

### T2 — Jitter, and motion vectors that survive it

Halton(2, 3) on the projection in `RenderContext::Draw` when the target has TAA on; `jitter` and
`prevJitter` on `ViewData` and `DrawData`; the offset carried across frames on `ViewMatrices` beside
the matrices `AdvanceCamera` already holds; `ComputeMotionVector` subtracts both before differencing.

*Gate:* extend `MotionVectors_test.cpp`. With jitter on, a static camera over a static surface must
still report zero velocity to within the `RG16_FLOAT` floor, and a translating camera must report the
same velocity it reports with jitter off. This is the assertion that catches a sign error or a missed
subtraction, and nothing else will.

### T3 — The resolve

The `RGBA16_FLOAT` history ping-pong with its RTVs and SRVs, the velocity buffer's missing SRV, and
`TaaResolvePass` — sampling `sceneColor`, the previous history and `motionVectors`, with a 3×3 YCoCg
neighbourhood clamp, tonemapped weighting, an exponential blend, and a history-invalid path that takes
`sceneColor` whole. It writes the new history and nothing else; `PostProcess` is repointed at that
history instead of `sceneColor` when the target has TAA on.
`docs/taa.md` and the `docs/passes.md` catalog entry land here, because this is the frame TAA first
exists in.

*Gate:* `AliasEnergy` across the quad's tilted edge falls from 0.0056 unjittered to 0.0023 converged
— the antialiasing claim as a number, with the unjittered value asserted to be aliased in the first
place so the comparison cannot be vacuous. `MeanColor` on the flat interior converges to the
unjittered value exactly (0.792157 either way), which is what catches an accumulation that converges
to *something* — darkened, tinted, drifting — rather than to the truth. And the first frame matches
the unjittered one, which is the cheapest check that the history-invalid path exists. Plus
`just run bgl_tests -- --gpu-validation` for the new SRVs and the resolve's barriers.

**No golden for the converged frame.** The plan asked for one; the two measurements above are
stronger and do not need regenerating when the scene changes, and a stored image would have pinned
the blend weight as if it were a contract. The pan case is also not here — the reprojection sign is
already pinned by T2's `MotionVectors_test`, which asserts velocity against a CPU-computed
expectation, and a smear test on top of that would be measuring the same arithmetic through a blurrier
instrument.

### T4 — The editor turns it on where it should be on

TAA for the viewport windows, off for `AssetThumbnailCache`, and a toggle so the two can be compared
without a rebuild. Whatever the frame loop needs so a still camera keeps producing frames, since a
history that stops updating stops converging.

*Gate:* `editor_tests`, plus a screenshot of a static scene held for a second showing edges resolved
and no crawl.

### T5 — Hashed alpha: the shader, the PSO pair, and the layer that reaches them

`util/HashedAlpha.slang` (the three-level world-space hash with screen-space derivatives, seeded off
the jitter index), `Forward_PBR_HashedAlpha` and its loose twin, and two `PsoType` rows in the opaque
shape.

**Correction to the split this plan first drew.** T5 was to be dead scaffolding with T6 carrying
`LayerType::kHashed`, but the two cannot separate that way: a `PsoType` is only reachable through
`GetPsoFromGeomAndMaterial`, which switches on `LayerType`, so with no enum value nothing — including
the test below — can drive the new pipeline at all. T5 therefore also adds `LayerType::kHashed` and
its `SubmeshPso` mapping, which is the smallest thing that makes its own gate runnable. T6 keeps what
is genuinely separable: persistence and authoring.

*Gate:* a `bgl_tests` case driving the new PSO directly over a known alpha ramp. `MeanColor` over a
flat-alpha patch gives the survival fraction, which must equal alpha within the sampling error, and it
must differ frame to frame — that is the whole contract, and both halves matter. Then `AliasEnergy`
over the same patch across a converged TAA run, falling as the frames accumulate: this is the
assertion that the noise is being *integrated* rather than merely produced. No golden here — a single
frame of stochastic coverage is noise by design and would be a golden of a random seed.

### T6 — `kHashed` through the authoring chain

`.bmaterial` and the assetlib cook, and the material editor's output node so a hair material can be
switched to it. The enum and its PSO mapping come in T5, which cannot be tested without them.

*Gate:* `assetlib_tests` on the round-trip, `gamelib_tests` on the resolved `MaterialHandle`, and a
render test asserting an instance authored `kHashed` lands in the new bucket.

### T7 — The hair, judged

Author the hair material as `kHashed`, capture it against the `occlude` original, and correct this
plan against what the images actually show. If the neighbourhood clamp is eating the strand highlights
or disocclusion is smearing, closest-fragment dilation and a depth SRV become a task here rather than
an assumption in T3.

*Gate:* `AliasEnergy` over the hair region, `kHashed` under converged TAA against the `occlude`
original, plus the before/after pair in the PR body at rest and under camera motion. The numeric part
matters because "looks better" is what this whole branch is being judged on and is the one claim a
screenshot cannot settle on its own. It also measures how much of the reported ghosting was the
missing velocity, which is what sizes T8.

### T8 — whatever ghosting survives the hair fix

Variance clipping in place of the min/max box, and closest-fragment velocity dilation if the images
still ask for it — the latter needs the depth SRV, so it is the task that pays for the thing T3
deferred. Not cut until T7 has measured, because tuning a clamp against an artifact that is about to
change shape is wasted work.

*Gate:* `AliasEnergy` and a pan case over the hair, against T7's numbers rather than against T3's.

### T9 — retire the `occlude` path

Gated on T7 saying hashed alpha wins. First the thumbnail cache converges — more than one warm-up
frame, and TAA on — because that is what makes a stochastic material safe everywhere. Then `occlude`
goes, and takes with it two `PsoType` rows, the two prepass kernels, `Forward_Transparent_Prepass`,
the `[occlude][plain]` partitioning in `TransparentSortPass` (which collapses the transparent phase
from three indirect dispatches to one), the `occlude` bool on `MaterialHandle` and `BMaterial`, and
the material editor's checkbox.

*Gate:* the thumbnail goldens, which are what would catch a warm-up count too low for the material to
converge; and the hair unchanged from T7's captures once its material is `kHashed` either way.
