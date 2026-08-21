# Bone viewer — the skeleton as a toggleable overlay

## Context

Nothing in the engine draws a joint. A rig's pose is only ever visible as the surface it deforms, so
what a skeleton is doing can be inferred but never read: a limb that lags, a root that drifts, a bone
that scales wrongly all reach the eye as a deformed mesh, and a bone that does nothing at all reaches
it as nothing.

Cross-fade blending is next on the rig's roadmap (`ROADMAP.md`, *Cross fade blending — slerp local
rotations then walk the hierarchy, never blend model space*), and it is the case that makes the gap
expensive. A blend is an operation on joint rotations; its failures — a limb taking the long way
round a slerp, a blend done in model space, a transition that snaps a frame before it should — are
statements about joints. Scrubbing a blend with only the skin to look at is reading the answer
through the thing that obscures it. The overlay is the instrument that work needs, and it wants to
exist before the blending does, not after.

## Decisions

**ADR-1 — The overlay draws the GPU bone palette, not a CPU re-derivation.** `SkinnedPosePass`
already writes every skinned instance's palette each frame, GPU-resident and per-instance indexed;
the overlay reads that. So the skeleton on screen *is* the skeleton that skins the mesh, and a pose
bug cannot hide in the gap between them. *Rejected:* evaluating `assetlib::poseModelTransforms` in
the editor and placing one instance per bone. bgl has no way to move an instance
(`ISceneView` creates a placement with a transform and never updates it), so every frame would
destroy and recreate ~100 instances; the CPU would have to reproduce the pose pass's inter-frame
interpolation exactly or the overlay would drift from the mesh by a fraction of a frame; and it
fights *GPU-driven by default*. Decisively, once blending lands, a CPU copy would have to learn to
blend too — and a second implementation of a blend is the last thing an instrument for checking
blends should contain.

**ADR-2 — Nothing new is uploaded.** `idl::SkinnedGeom` already carries the rig's bone table, each
`idl::SkinnedBone` holding `inverseBind` and `parent`; `idl::SkinnedState` names the instance's slice
of the palette buffer. A joint's model-space position is its palette entry applied to its bind-pose
model position, and that bind position is the inverse of `inverseBind` applied to the origin. Every
term is already there, so the client supplies nothing and the toggle is the entire new API.
*Rejected:* uploading each bone's bind-pose model transform beside the table — 64 B a bone of
permanent GPU memory to avoid one affine inverse per bone per frame, on a pass that runs only while
a human is looking at it.

**ADR-3 — Two stages: joints, then solids.** A compute step writes each bone's joint position and
roll basis into a per-view buffer; the raster step reads that buffer and emits geometry. *Rejected:*
deriving the joints inline in the mesh shader — one stage and one buffer fewer, but then nothing in
the frame is readable, and a golden image cannot distinguish a wrong inverse bind from a wrong
palette slice from a wrong octahedron. `SkinnedPose_test.cpp` reads the palette off the GPU for
exactly this reason, and says so in its own header comment.

**ADR-4 — `PostProcess` composites the overlay; the pass does not touch the backbuffer.**
`BoneOverlayPass` renders its solids into a colour target of its own at *output* resolution, and
`PostProcess` blends that over the tonemapped frame exactly as it already blends the outline mask —
`PostProcessPass::Args` carries a mask SRV, the grid it was authored on and an enable flag, and the
overlay is one more of each. Two things fall out of compositing there rather than earlier: `TaaResolve`
never sees the overlay, so thin geometry that writes no motion vectors cannot ghost through it; and
the overlay is authored on the output grid, so a render scale below 1 does not soften it. It carries
colour and coverage rather than the outline's R8 mask, because the solids shade themselves and
`PostProcess` only has to blend. *Rejected:* drawing the pass straight onto the backbuffer between
`PostProcess` and `PreparePresent`. One transient texture and one tap cheaper, and it would end
`PostProcess` being the backbuffer's only writer — the rule `docs/passes.md` records to keep
`SubmitCapture`'s readback describing what was displayed — to save a resource that exists only while
somebody has the checkbox on. *Rejected:* drawing into `sceneColor` beside the forward pass, which is
where TAA smears it and the render grid softens it.

**ADR-5 — X-ray against the mesh, self-occluding among itself.** The pass owns a depth texture of its
own, cleared each frame, so bones occlude each other and never test against the scene. *Rejected:*
testing the scene's depth — every bone of a clothed character is inside its skin, so the overlay
would be invisible in exactly the case that motivates it. *Rejected:* no depth at all — an octahedron
is convex, so backface culling draws one bone correctly, but the far arm's bones then paint over the
near shoulder's and the picture reads as noise on a dense rig.

**ADR-6 — One toggle, on the render target.** `IRenderTarget::SetBoneOverlayEnabled(bool)`, off by
default, mirroring `SetOutlineEnabled`. *Rejected:* per-instance opt-in on the view, the shape
`SetSubmeshSelected` has. That is what a level viewport will want when it is showing one selected
unit out of thousands, and it is a distinction the Animation panel — one rig, always — cannot
express. It can be added later as a refinement without changing this switch.

**ADR-7 — The checkbox is live only on the skinned tier of a mesh that has clips.** A `.bvat` carries
its baked palettes in the container (`BVat::palettes`) and nothing on the GPU reads them, so the VAT
tier has no bones at draw time; a mesh with no clip file loads as static geometry and has no palette
at all. Both grey the box and say why in its tooltip. *Rejected:* uploading the VAT side-channel as a
second pose source for this pass. It is the roadmap's own item — the first consumer it names is
attachments and the VAT→skeletal death handoff — and it deserves that feature rather than riding in
as a fourth task here.

## Non-goals

- **Bone names or any text in the viewport.** bgl renders no text anywhere; it would be its own
  feature.
- **Picking, hovering or selecting a bone**, and any per-bone colour that would exist to show a
  selection. The overlay is read-only.
- **Editing a pose in the viewport** — no dragging joints, no IK handles. The panel previews.
- **The level viewport.** The target toggle will work there unchanged, but no checkbox is wired into
  it by this feature.
- **Bones on the VAT tier**, per ADR-7.
- **Blending itself.** This is the instrument, not the feature it is for.

## Acceptance

- `bgl_tests`, readback: a three-bone chain posed at its bind frame and at a swung frame; the joint
  positions the overlay's compute step wrote equal the translations of
  `assetlib::poseModelTransforms` for the same frame. A bind-pose frame must reproduce the bind
  positions exactly, which is the cheapest check that the inverse binds and the palette agree.
- `bgl_tests`, differential: the same posed rig rendered twice in one run, overlay off then on, the
  two frames compared and the luma probed at the projected position of each joint. No committed
  `.exp.png` — `SelectionOutline_test.cpp` and `SkinnedRender_test.cpp` both refuse one on the
  grounds that it blesses whatever the code produced, and the overlay's whole claim is *where* the
  joints are, which a probe states and an image only implies.
- `bgl_tests` under `--gpu-validation`: clean. The feature adds a pass, a depth resource and a
  buffer, so the barrier derivation is new and unproven.
- `editor_tests`: the enable rule as a free function — skinned tier with clips enables, VAT disables,
  no clips disables — so the panel's one piece of logic is pinned without a device.
- **Not verifiable from this machine:** the checkbox's placement and appearance. Terminal here has no
  Screen Recording permission, so no editor screenshot can be taken; the panel's layout is reviewed
  by eye on a Windows checkout, and no PR in this feature may claim otherwise.

## What the survey found

- `SkinnedPosePass` ([libs/bgl/src/passes/SkinnedPosePass.h](libs/bgl/src/passes/SkinnedPosePass.h))
  writes two palettes per skinned instance each frame, at `time` and `prevTime`, one workgroup per
  instance. `docs/skinning.md` states the palette buffer is GPU-written and has no CPU mirror.
- `idl::SkinnedBone` ([libs/bgl/idl/src/SkinnedBone.slang](libs/bgl/idl/src/SkinnedBone.slang)) holds
  `inverseBind`, `parent` (`cInvalidBone` for a root) and `depth`; bones are topologically sorted, so
  a parent's joint is always resolved before its child's. `idl::SkinnedState`
  ([libs/bgl/idl/src/SkinnedState.slang](libs/bgl/idl/src/SkinnedState.slang)) carries the instance's
  palette offset, `cFloat4sPerBone` = 3 float4s a bone.
- `OutlineMaskPass` ([libs/bgl/src/passes/OutlineMaskPass.h](libs/bgl/src/passes/OutlineMaskPass.h))
  is the precedent for an editor-only visual: a pass attached only when there is something to draw,
  gated by `IRenderTarget::SetOutlineEnabled` ([libs/bgl/include/bgl/IRenderTarget.h](libs/bgl/include/bgl/IRenderTarget.h)),
  composited by `PostProcess` after the tonemap.
- Frame order is in [docs/passes.md](docs/passes.md) § The frame. `PostProcess` is named there as the
  backbuffer's only writer, and the rule earns its keep: it is what makes `SubmitCapture`'s readback
  describe what was displayed. `PostProcessPass::Args` is already shaped for a composited overlay —
  `outlineMask`, `maskSampler`, `maskSize`, `outlineEnabled`
  ([libs/bgl/src/gfx/RenderContext.cpp](libs/bgl/src/gfx/RenderContext.cpp)).
- bgl has no line topology and no caller-coloured unlit material. `GeomType` is
  `kStaticMesh | kVatMesh | kSkinnedMesh`; a null material draws through `Forward_Null` at a literal
  `1.0`. The overlay therefore carries its own pipeline and shades its own solids, and adds no
  `GeomType`.
- Buffer readback exists ([libs/bgl/src/resource/Readback.h](libs/bgl/src/resource/Readback.h)) and
  `SkinnedPose_test.cpp` already uses it to check the pose pass stage by stage.
- The house idiom for a visual test is **not** a committed golden: `SelectionOutline_test.cpp` and
  `SkinnedRender_test.cpp` render the frames they compare within one run — off against on, skinned
  against a static reference — and probe luma at projected world points. `test::FrameDelta` is
  shared ([libs/bgl/tests/src/util/GoldenImage.h](libs/bgl/tests/src/util/GoldenImage.h)); the
  world-point probe is a file-local `LumaAt` in `SkinnedRender_test.cpp`, so task 2 lifts it beside
  `FrameDelta` rather than writing a second one. `assets/golden/` holds `.exp.png` for the static and
  PBR paths only, and nothing for skinning or the outline.
- The Animation panel owns the clock and pushes it through `RenderTargetWindow::SetTime`;
  `AnimationPreviewWindow` spawns instances `{clip, phase 0, rate 1}` and re-loads on a tier switch
  ([apps/editor/src/Windows/AnimationEditor/AnimationPreviewWindow.h](apps/editor/src/Windows/AnimationEditor/AnimationPreviewWindow.h)).
  `PlanAnimationLoad` ([.../animation_draws.cpp](apps/editor/src/Windows/AnimationEditor/animation_draws.cpp))
  is where "no clip file means static geometry" is already decided, and is the neighbour the enable
  rule belongs beside.
- `gamelib` does not retain the `assetlib::Skeleton` after `AcquireSkinnedMesh` — it loads one,
  validates against it and hands bgl the containers. Nothing in the editor needs it back under ADR-1.

## What changes

| | |
|---|---|
| `libs/bgl/idl/src/` | one new struct for a joint record (position, roll basis, parent joint), so the compute and raster stages share one definition |
| `libs/bgl/shaders/src/` | the joint-derivation kernel and the overlay's mesh/pixel stages |
| `libs/bgl/src/passes/BoneOverlayPass.{h,cpp}` | the new pass, both stages, its own depth |
| `libs/bgl/src/passes/PostProcessPass.{h,cpp}` and its shader | an overlay input beside the outline mask: one SRV, its grid size, its enable flag |
| `libs/bgl/src/gfx/RenderContext.cpp` | owns the pass; attaches it before `PostProcess` when the target has the toggle on and the view has a skinned instance, and feeds its result into the composite |
| `libs/bgl/include/bgl/IRenderTarget.h` | `SetBoneOverlayEnabled` / `IsBoneOverlayEnabled`, mirroring the outline pair |
| `docs/passes.md`, `docs/skinning.md`, `docs/bgl_api.md` | the catalog entry, the frame diagram, what `PostProcess` now composites, and the toggle |
| `apps/editor/src/Windows/AnimationEditor/` | the checkbox, its enable rule, and forwarding to the target |

What could break: `PostProcess`, which gains a second optional input and must still composite the
outline correctly beside it; and TAA, if the overlay is ever attached inside the view subgraph by
mistake. The frame rendered with the overlay off is what holds both lines — it must be identical to
one from a build that has none of this.

## The tasks

**1 — `bgl`: joint positions off the palette.** The IDL joint record, the compute stage, the
per-view buffer it writes, and `IRenderTarget::SetBoneOverlayEnabled` gating whether the stage runs.
Nothing is drawn: this task lands scaffolding, and the tests are its only caller. *Gate:* the
readback test above — bind frame and swung frame against `assetlib::poseModelTransforms`, plus a
scene with no skinned instances leaving the buffer untouched and the stage unattached.

**2 — `bgl`: the solids.** The mesh and pixel stages: one octahedron per bone, from its parent's
joint to its own, rolled by the parent's palette rotation, with a fallback shape for roots and for a
zero-length bone. Its own depth texture and output-resolution colour target, composited by
`PostProcess`. *Gate:* the off/on frame pair
with luma probed at each projected joint, an unchanged frame where the target has no skinned
instance, and `--gpu-validation` clean. `docs/passes.md`'s frame diagram and single-writer rule are corrected
in this task's commit.

**3 — `apps/editor`: the checkbox.** *Show bones* in the Animation panel, forwarded to the preview's
render target, greyed with a tooltip on the VAT tier and on a mesh with no clips. *Gate:*
`editor_tests` on the enable rule as a free function beside `PlanAnimationLoad`; the layout itself is
stated as unseen.
