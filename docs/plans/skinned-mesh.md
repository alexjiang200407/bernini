# Skinned meshes at runtime

## Context

`assetlib` already imports and stores rigs in full. A glTF with a skin yields a `Skeleton` whose
bones carry `bindPose`, `inverseBind` and a `parent` that is always a lower index
([`Skeleton.h:8-12,18`](../../libs/assetlib_structs/include/assetlib_structs/Skeleton.h)), an
`AnimationSet` whose clips are resampled to a fixed rate and stored frame-major as local TRS
([`Animation.h:40-46`](../../libs/assetlib_structs/include/assetlib_structs/Animation.h)), and a
`.bmesh` whose submeshes carry `joints0`/`weights0` already remapped into skeleton bone order. All
of it round-trips through `.bskel` / `.banim`, and the editor's importer writes both beside the mesh.

**Nothing at runtime reads any of it.** `grep` finds no bone, joint or skinning code in `bgl`,
`gamelib` or the editor's render path; `GeomType.h:10` is still `//kSkinnedMesh,`, commented out.
The only prepared seam is `VertexLayout.slang:9-10`, which names `kJoints0` and `kWeights0` and a
`kUint16x4` format to hold them.

So every animated thing on screen must first go through `bakeVat`. Two costs follow. The editor can
only preview a rig through a build product — a `.bvat` that has already lost the skeleton, and whose
bake is where an authoring mistake gets silently absorbed. And the hero tier has no path at all:
IK, look-at, per-bone variation and attachments are all things VAT structurally forecloses
(`ROADMAP.md:134`), so a unit that needs any of them cannot be drawn today.

The import half landed with the VAT work. This is the runtime half, and it is the next unstarted
block in the roadmap's Animation section (`ROADMAP.md:137-147`).

## Decisions

**ADR-1 — the pose is evaluated on the GPU.** Skeleton and clip samples upload once as scene
buffers; a compute pass writes each instance's bone palette every frame, one workgroup per instance
and one thread per bone. *Rejected:* evaluating on the CPU in `gamelib` by reusing
`assetlib::poseModelTransforms` / `skinningMatrices`, which already exist and are already tested. It
is far less new code, but it is per-unit per-frame CPU work — the thing the Guiding Constraints name
as the enemy — it does not survive contact with a crowd, and every line of it is deleted by the
first GPU pass that replaces it.

**ADR-2 — skinning is applied in the mesh shader,** reading the palette at vertex-decode time.
*Rejected:* a compute pre-pass skinning into a transient vertex buffer. `ROADMAP.md:143` reserves
that for the hero tier specifically, and there is no consumer for it until something needs to read
skinned positions back (physics, attachments). Adding it now buys nothing and costs a transient
allocation per instance per frame.

**ADR-3 — `RenderJob::time` stays the only per-frame input.** An instance is spawned with
`{clip, phase, rate}` and never touched again, exactly as `VatInstanceDesc` is
(`bgl/InstanceDesc.h`). *Rejected:* a CPU-driven per-instance time, which would fork the editor's
transport from the game clock and re-introduce the per-unit CPU update ADR-1 refuses.

**ADR-4 — the bgl seam takes `assetlib_structs` types directly.** `bgl` links `assetlib_structs`
publicly (`libs/bgl/CMakeLists.txt:59`) and `IScene::AddStaticMeshGeom` already takes an
`assetlib::BMesh`; `Skeleton` and `AnimationSet` live in that same target. So
`AddSkinnedMeshGeom` takes them as they are. *Rejected:* mirroring them into bgl-owned PODs the way
`VatGeomDesc` mirrors a decoded `.bvat`. That indirection exists because a `.bvat` decodes to GPU
textures and bgl must stay codec-free; a `Skeleton` is already a POD and needs no decode, so the
mirror would be duplication with a drift risk and no layering benefit.

**ADR-5 — the previous pose is re-evaluated at `prevTime`, not remembered.** The pose pass writes
two palettes per instance in one dispatch: one at `viewData.time`, one at `viewData.prevTime`. The
mesh shader skins both and hands the pixel stage both clip positions at the seam
`forward/common.slang:25` already anticipates, so skinned geometry writes real motion vectors.
*Rejected:* a true double buffer holding last frame's palette, which is what `ROADMAP.md:145`
assumes. Re-evaluation is what the VAT path already does (`Forward_VatMesh.slang:135`), it is
correct on the first frame and on an instance spawned mid-frame where a history buffer holds
garbage, and it needs no ping-pong. Its one weakness is that it is only correct while time is the
sole input to the pose — a mid-frame clip switch would reproject through the wrong clip. That is
outside this feature (no state machine, no blending), and the constraint is recorded in
`docs/skinning.md` for whoever adds one.

**ADR-6 — a skinned mesh reuses the existing `.bmesh` vertex stream.** `joints0`/`weights0` are
already optional attributes of the same interleaved buffer, arriving together or not at all
(`docs/asset_standards.md:229-230`). *Rejected:* a separate skinned vertex buffer, which would
duplicate position/normal/uv for every rigged mesh and fork `DecodeVertex`.

**ADR-7 — the palette *stores* three rows a bone; the walk *composes* in `float4x4`.** The fourth
row of a skinning matrix is always `(0,0,0,1)`, so storing it wastes a quarter of the largest
per-instance allocation in the frame — doubled again by ADR-5. But the walk composes through `mul()`,
and giving it a second matrix shape would mean reasoning about a packed layout at every step, so the
groupshared transforms stay `float4x4` and only the buffer is packed. *Rejected:* `float4x4`
throughout (33% more memory for nothing), and `float3x4` throughout (a bespoke affine multiply, on a
matrix convention the rest of the shaders do not use). The cost of the split is that
`cMaxBonesPerRig` is sized by the *unpacked* form: 192 bones at 64 bytes is the 12 KiB groupshared
budget, where the plan originally assumed 256 at 48.

**ADR-8 — inter-frame blending is nlerp with hemisphere correction,** not slerp. Clips are
resampled to 30 Hz, so the rotation between adjacent frames is small and nlerp's error with it.
*Rejected:* slerp, which is exact but spends transcendentals per bone per instance per frame, twice
over under ADR-5.

**ADR-9 — the Animation panel previews skinned or `.bvat`, switchable.** *Rejected:* skinned
replacing VAT preview. Being able to A/B the two is how a bad bake gets caught, and it is the
skinned↔VAT comparison `ROADMAP.md:146` wants, arriving early and nearly free.

**ADR-10 — one `idl::Clip` and one clip buffer for every animated tier.** A clip's frame span,
authored rate and loop flag mean the same thing to VAT and to a skinned rig, so `firstFrame` is "where
this clip's frame 0 sits in the tier's own frame space" — a texture row for VAT, a frame of the
frame-major sample pool for a skinned rig — and both allocate out of one `scene.clipBuffer`.
*Rejected:* a `SkinnedClip` byte-identical to `VatClip` in a second buffer of the same element type,
which is what the plan originally described. It duplicates the struct the roadmap's per-clip metadata
(root motion, locomotion speed, notifies) will have to grow, and gives the scene a second arena to
grow for no gain. The cost is that `firstFrame`'s *unit* depends on the tier; the alternative was two
structs that would drift.

## Non-goals

- **Crossfade and multi-clip blending.** One clip at a time. The pose pass may be *shaped* so a
  weighted clip list widens into it later, but it takes one clip now.
- **GPU skinning into a transient vertex buffer** (ADR-2) — hero tier, no consumer yet.
- **Bone masks, additive layers, IK, look-at, root motion.** All listed separately in the roadmap.
- **The skinned→VAT LOD swap and any automatic tier selection.** A caller chooses which it acquires.
- **Level-viewport playback of skinned instances.** The level viewport has no clock; the roadmap
  lists that as open for VAT too, and it drags in the outline-mask kernel.
- **Rotation compression, per-LOD bone sets, the state machine.**
- **Any change to the import path.** `.bmesh`, `.bskel` and `.banim` are consumed exactly as they
  are written today. Anything the GPU needs that is not in them (per-bone depth) is derived at
  upload, not added to a container.
- **Reading the `.bvat`'s baked palette side-channel.** It stays without a consumer.
- **Alpha-test, blended or loose materials on skinned geometry.** One opaque `kPBR` bucket, the same
  constraint VAT ships with (`util.cpp:140-143`).

## Acceptance

1. **Bind pose reproduces the static mesh.** A rig posed at a bind-pose frame through the skinned
   path renders pixel-identical to the same mesh drawn statically. `skinningMatrices` is identity
   at bind pose by construction (`skeleton.h:69-71`), so this single gate catches a wrong
   inverse-bind, a wrong hierarchy walk and a wrong weight normalisation at once, with no
   hand-authored expected matrices.
2. **`bgl_tests` palette readback.** A hand-built rig with a known hierarchy and a two-frame clip,
   dispatched and read back, asserted bone-for-bone against expected matrices at a fixed time —
   including a fractional frame, so the interpolation is pinned. This is the layer that *localises*
   a failure of gate 1.
3. **`bgl_tests` golden image** of a rigged mesh posed through the skinned path at a fixed time,
   plus a velocity assertion that an *animating* skinned instance writes non-zero motion vectors
   where the pose moves and a held one (`rate = 0`) writes camera-only velocity.
4. **`editor_tests`**: the Animation panel resolves a rig, drives it skinned, and toggles to the
   VAT source.
5. **GPU validation clean** on the pose-compute and skinned-forward shaders — Metal via
   `METAL_DEVICE_WRAPPER_TYPE=1 MTL_SHADER_VALIDATION=1`, D3D12 via `--gpu-validation`.

## What the survey found

**The VAT path is the template, and it is close to a one-for-one match.**

| VAT | skinned counterpart |
|---|---|
| `GeomType::kVatMesh` (`GeomType.h:9`) | `kSkinnedMesh`, already reserved on line 10 |
| `PsoType::kOpaque_VatMesh_PBR` (`PsoType.h:19`) | one new opaque PBR bucket |
| `VatGeom` / `VatClip` / `VatState` IDL (`libs/bgl/idl/src/`) | `SkinnedGeom` / `SkinnedClip` / `SkinnedState` |
| `IScene::AddVatMeshGeom` (`IScene.h:250,274`) | `AddSkinnedMeshGeom` |
| `ISceneView::CreateVatMeshInstance` + `VatInstanceDesc` (`ISceneView.h:50,64`) | `CreateSkinnedMeshInstance`, same three fields |
| `Forward_VatMesh.slang` | `Forward_SkinnedMesh.slang` |
| `AssetManager::AcquireVatMesh` / `CreateVatInstance` (`AssetManager.h:181,274`) | `AcquireSkinnedMesh` / `CreateSkinnedInstance` |

What differs is the middle: VAT reads a pose out of a texture with two `Load`s, and the skinned path
has to *compute* one first. That compute pass is the only genuinely new machinery in the feature.

**Facts the tasks depend on:**

- `bgl` links `assetlib_structs` `PUBLIC` (`libs/bgl/CMakeLists.txt:56-62`) and already takes an
  `assetlib::BMesh` through `AddStaticMeshGeom` (`IScene.h:217`). `Skeleton.h` and `Animation.h`
  are in that target. ADR-4 rests on this.
- Bones are topologically sorted, `parent(i) < i`, validated at import
  (`Skeleton.h:8-12`, `ROADMAP.md:92`). A hierarchy walk is therefore a forward pass with no sort —
  but a *parallel* walk needs a per-bone depth, which no container carries. It is derived in one
  pass at upload (`depth[i] = depth[parent[i]] + 1`), which is why this needs no format change.
- Samples are frame-major: bone `b` of frame `f` is `samples[clip.firstSample + f*boneCount + b]`
  (`Animation.h:40-41`), local TRS (`Transform` = vec3/quat/vec3, 40 B, `Node.h:9-16`).
- Clips span the closed interval `[0, duration]`, so a looping clip's last frame duplicates its
  first and "a runtime that plays both stutters by one frame" (`Animation.h:14-18`). VAT's bake
  handles this with a pad row; the skinned path must handle it in the frame-index arithmetic
  instead. This is the single likeliest source of a subtle one-frame hitch.
- `ViewData` already carries `time` and `prevTime` (`ViewData.slang:15-17`), fed from
  `RenderContext.cpp:505`. ADR-5 needs no new plumbing.
- `AnimationPreviewWindow` "has no clock of its own: the panel owns the transport and feeds
  SetTime" (`AnimationPreviewWindow.h:38`), and `PlaybackTransport` already produces exactly the
  seconds `RenderJob::time` wants. Under ADR-3 the transport needs **no change at all** — the
  editor task is acquisition and a source toggle, not new playback.
- `kVatMesh` has only eight consumers across `bgl` (`ISceneView.h`, `GeomType.h`, `util.cpp`,
  `Scene.{h,cpp}`, `SceneView.{h,cpp}`, one test). The blast radius of a third geom type is small
  and known.
- No `docs/specs/` file and no design doc covers skinning; there is nothing to reconcile against.

## What changes

| Area | Change | What could break |
|---|---|---|
| `libs/bgl/idl/src/` | `SkinnedBone`, `SkinnedClip`, `SkinnedGeom`, `SkinnedState`, `BoneSample` | IDL is the single source of truth; a hand-mirrored struct here is a silent GPU/CPU disagreement |
| `libs/bgl/include/bgl/` | `GeomType::kSkinnedMesh`, new `PsoType`, `AddSkinnedMeshGeom`, `CreateSkinnedMeshInstance` | `PsoType` is generated — `just idl`, and `c_PsoCount` sizes per-PSO arrays |
| `libs/bgl/src/scene/` | geom record's skinned half, `EntryBuffer<idl::SkinnedState>` beside `m_VatStates` (`SceneView.h:305`), palette allocation on `GrowableGpuBuffer` | the palette is the one per-instance allocation that scales with *bone count* as well as instance count, and ADR-5 doubles it |
| `libs/bgl/src/passes/` | `SkinnedPosePass`, ordered before `ForwardPass` | a missed FrameGraph read/write declaration means a missing barrier, not a compile error |
| `libs/bgl/shaders/src/` | `PoseSkinned.slang`, `Forward_SkinnedMesh.slang`, `forward/vertexdecode.slang` | `joints0`/`weights0` decode paths are untested today; unnormalised weights darken or bloat the mesh |
| `libs/gamelib/` | `AcquireSkinnedMesh`, `CreateSkinnedInstance` | refcount symmetry with the VAT acquire; a rig acquired both ways must not share a geom record |
| `apps/editor/` | source toggle, skinned acquisition in `AnimationPreviewWindow` | `SetActiveClip` respawns instances; the skinned path must respawn the same way |
| `docs/` | new `docs/skinning.md`; `geometry_layout.md`, `passes.md`, `ROADMAP.md` updated | — |

## Tasks

Bottom-up by layer, each with its gate.

**1 — `bgl`: the skinned geom and its GPU tables.** *(landed)* The IDL structs,
`GeomType::kSkinnedMesh`, and `IScene::AddSkinnedMeshGeom` uploading bone / clip / sample buffers,
with the per-bone depth derived at upload. Validates the two containers against each other, that
every submesh carries `joints0` and `weights0`, and the bone-count cap. **The instance half lands
here too** — `ISceneView::SkinnedInstanceDesc` and `CreateSkinnedMeshInstance`, writing an
`EntryBuffer<idl::SkinnedState>` beside `m_VatStates`. Both halves of the seam in one task, because
task 2 needs real `SkinnedState` records to dispatch against and task 3 needs placed instances to
render. Nothing draws it yet — dead scaffolding, justified because the tests call it.
*Gate:* `bgl_tests` builds a small rig, adds the geom, creates an instance, reads the geom tables
and the instance's `SkinnedState` back and asserts both; each documented refusal throws.

*Three things this task moved, and why:*
- **The `PsoType` bucket went to task 3.** `ForwardPass` holds a `std::array<PsoConfig, c_PsoCount>`
  under a `static_assert` that every row names a pixel shader, and `Init` builds a kernel for every
  PSO at device bring-up — so a bucket declared before `Forward_SkinnedMesh.slang` exists fails every
  test that creates a device. A skinned submesh resolves to `PsoType::kInvalid` until task 3, which
  is the value the counting sort already skips, so the instance uploads and simply draws nothing.
- **The palette went to task 2.** It is written by the GPU, so it is not a CPU-mirrored
  `RangeBuffer` like everything else here; it belongs with the pass that writes it rather than with
  the tables that are uploaded.
- **The skeleton-signature check went to task 4.** Computing a `Skeleton`'s signature needs
  `assetlib`, which `bgl` does not link, so `bgl` can only check that the bone counts agree. The
  signature is a stale-cook check — a loader's concern — and `gamelib`'s acquire is where it belongs.

*And two things review added to it:* the `Clip` merge of ADR-10, and moving the instance descs out of
`ISceneView` into `bgl/InstanceDesc.h` so they are `bgl::VatInstanceDesc` rather than
`bgl::ISceneView::VatInstanceDesc`.

**2 — `bgl`: the pose compute pass.** *(landed)* `PoseSkinned.slang` — workgroup per instance, thread
per bone (strided above the group size), clip sampled at `time` with nlerp between the two frames it
falls between, hierarchy walked by depth level with a barrier per level, multiplied by `inverseBind`,
written three rows a bone. Dispatched twice per instance for `time` and `prevTime` (ADR-5). Owns the
`BonePaletteBuffer` and the slice each instance holds in it. `SkinnedPosePass` ordered ahead of
`ForwardPass`. Still nothing draws.
*Gate:* **acceptance 2** — palette readback asserted bone-for-bone.

*Two things this task had to fix rather than add:*
- **A drawable with no pipeline is no longer enqueued.** Task 1 left a skinned submesh resolving to
  `PsoType::kInvalid` on the reasoning that the counting sort skips it. It does — but
  `HistogramInstances.slang:38` `dbg_assert`s on a pso past the bucket count *before* skipping, so the
  first frame that rendered a skinned instance aborted at teardown on a recorded GPU assertion. No
  task-1 test drew a frame, which is why it was latent. `WritePlacement` now enqueues a
  `SubmeshInstance` only when its pso names a real bucket, keeping the slot so `submeshIndex` still
  addresses it. That is a general invariant, not a skinned special case, and it stops being reachable
  at all once task 3 gives the tier a pipeline.
- **The pass is attached under the *view's* namespace, not a cull namespace.** The graph decides a
  pass is a root by whether it writes an imported resource, and a name resolved inside a cull
  namespace matches no import — so attached where the plan implied (beside `ForwardPass`, after
  `SetResourceNamespace(cullNamespace)`) the pass was silently culled and never ran. It belongs
  outside that scope anyway: a palette is per instance, not per frustum.

**3 — `bgl`: draw it.** *(landed)* `PsoType::kOpaque_SkinnedMesh_PBR` and its `c_Psos` row, and
`Forward_SkinnedMesh.slang`: decode `joints0`/`weights0`, four palette matrices, linear blend on
position, normal and tangent; the `prevTime` half of the palette slice gives the previous clip
position at the `common.slang:25` seam, so motion vectors fall out. `ForwardPass` binds the skinned
buffers through a `skinnedData` cbuffer and `util.cpp` maps the bucket.
*Gate:* **acceptance 1 and 3**, GPU validation run (**acceptance 5**).

*One deliberate substitution:* **no golden image was committed.** Acceptance 3 asked for one, but a
golden blesses whatever this code produced — it can only catch a later regression, never an error
present when it was minted. The bind-pose case has something strictly better available: the *static*
path renders the same `.bmesh` bytes and was correct before any of this existed, so the gate is a
whole-frame `FrameDelta` between the two, which is an independent reference. The posed frame, which
has no such reference, is pinned by luma probes at positions derived from the rig's own arithmetic
(where a 90-degree swing about a known pivot must and must not put geometry), the way
`VatPlayback_test` pins playback. A golden remains worth adding when there is a rig whose correct
appearance a human has actually looked at.

**4 — `gamelib`: acquisition.** *(landed)* `AcquireSkinnedMesh(relPath, animationsRelPath,
meshIndex)` reading `.bmesh` + `.bskel` + `.banim` and returning a geom plus a clip table, and
`CreateSkinnedInstance`. No bake and no freshness rule — unlike VAT there is no derived product, the
containers *are* the source, which is most of why this task is small. It owns the
`skeletonSignature` check, which `bgl` cannot make (see task 1).
*Gate:* `gamelib_tests` acquires a fixture rig, shares the geom on a second acquire, and releases to
zero; a mismatched skeleton signature is refused.

*Two things worth recording:*
- **A caller never names the skeleton.** The clip set names its own rig
  (`AnimationSet::skeleton`), so `AcquireSkinnedMesh` follows it rather than taking a third path.
  A mesh, a rig and a clip set cannot be paired wrongly by hand — only by a rig that changed after
  the clips were cooked, which is exactly what the signature catches.
- **`VatClipInfo` became `ClipInfo`.** Both tiers hand back the same clip description, and a caller
  showing a clip list should not have to know which door it came through — the same consolidation
  ADR-10 made for `idl::Clip`. The rig fixture the acquire suites share moved to
  `libs/gamelib/tests/src/util/RigFixture.h` for the same reason: both need a rig on disk, and it
  was 150 lines.

**5 — editor: the Animation panel plays skinned.** *(landed)* A "Preview As" selector (Skinned /
VAT) on the panel; `AnimationPreviewWindow` acquires through whichever is selected and respawns on a
clip change the way it does today. The transport is untouched (ADR-3), which was the point of ADR-3.
Switching re-loads, because the tiers are different uploads.

*Gate:* **acceptance 4, only partly.** The tier-dependent decisions are extracted into
`PlanAnimationLoad` and pinned by `editor_tests` — that the skinned tier does not bake (seconds of
CPU skinning for a texture pair it never samples, and it would make the preview need a *bakeable*
material), does not frame by the bake's box, and does not offer a bake to answer a refusal.

What is **not** covered is the panel itself: constructing it, loading a rig, toggling. That is a
pre-existing, already-documented gap rather than one this task introduced --
`apps/editor/CLAUDE.md` names `AnimationPreviewWindow` and `MainWindow` as **not covered**, because
`RenderTargetWindow`'s constructor calls `CreateRenderTarget` with a real `winId()` and
`headless = false` and does not guard a null device. The doc prescribes the fix: **a `headless` flag
on `RenderTargetWindowDesc`**. (The loading screen is *not* the obstacle -- `editor::test::OnLoadingScreen`
already handles it.)

That seam is shared by every render-target window, so it belongs in its own change rather than riding
in with a panel feature -- see task 5c. Meanwhile the panel was smoke-run (the editor launches, builds
its targets and runs with a clean log) and every layer beneath it is covered by `bgl_tests` and
`gamelib_tests`.

**5d — bone tags, and a camera that uses them.** *(added after running the panel.)* The Animation
panel opens at a fixed yaw, which shows a rig a profile whenever its forward axis is not the one that
yaw assumed -- the test coyote faces +X where glTF's convention is +Z, so it opens side-on and the
user orbits once.

Deriving the facing from bone *names* was tried and removed: it worked on the coyote but only because
that rig is named in English, and a heuristic that guesses which bone is a head is the wrong shape for
something an author can simply state. The replacement is to let them state it -- display the skeleton
in the editor and let a bone carry a tag ("head", "root", "attachment") -- and read the camera's
facing off that.

Bigger than it looks, and genuinely its own feature: `Bone` has no tag field, so it needs a `.bskel`
format change, the importer writing it, editor UI, and an answer to what happens to a user's tags when
the `.bskel` is re-cooked from its glTF -- `.bskel` is a derived file, so tags have to survive a
re-import or live beside it. That last question is the design, not the UI. Crosses this feature's
"no change to the import path" non-goal, which is why it is not done here.
*Gate:* a rig whose bones carry a `head` tag opens facing the camera, whatever axis it faces, with no
name matching anywhere in the path.

**5c — a `headless` seam on `RenderTargetWindowDesc`.** *(added by review of task 5, and task 5's
own review is the argument for it: the tier switch shipped a bug where the geom was acquired through
the new tier while the instance was created through the old one, which every automated gate passed
and only a reading caught. A toggle is the one thing no test in the tree can perform.)* Every
render-target window is untestable today because the constructor demands a real `winId()`, which is
why `apps/editor/CLAUDE.md` lists `AnimationPreviewWindow` and `MainWindow` as uncovered. A `headless`
flag on the desc -- the fix that doc already prescribes -- unblocks a real panel test, which is what
closes acceptance 4 properly, and unblocks the other windows with it. Independent of the skinned path;
listed here because task 5 is what made the gap concrete.
*Gate:* an `editor_tests` case that constructs `AnimationPreviewWindow` headless, loads a synthesized
rig, and toggles the tier -- acceptance 4 as originally written.

**5b — the scene's buffer registration stops being positional.** *(added by review of task 1.)*
`Scene::ImportResources` walks `GetBuffers()` with `std::apply` and takes each buffer's FrameGraph
name positionally out of a parallel `c_BufferInfo` array; `SceneView` has the same pair. Adding a
buffer means editing two lists in lockstep and a test's structured bindings, and nothing catches a
mis-pairing — a buffer would simply be imported under its neighbour's name. Every buffer already
takes a `debugName` at `Init`; giving it its graph name too lets `ImportResources` ask the buffer and
deletes both parallel arrays. Independent of the skinned path — it is only listed here because this
feature is what made the tedium visible.
*Gate:* both arrays gone, `just test` unchanged, and one test that a buffer reports the name it was
initialised with.

**6 — docs, and the plan comes out.** *(`docs/skinning.md` was created early, in task 4's review: a
reviewer asked for a 24-line javadoc's reasoning to move into docs, and there was nowhere to put it.
This task extends it rather than writing it.)* `docs/skinning.md` as the subsystem page — the pose pass, the
palette layout and its lifetime, the `prevTime` constraint from ADR-5, the interface→file table;
`geometry_layout.md` and `passes.md` amended. `ROADMAP.md` ticked — and line 145 **reworded**, not
just checked: it currently says skinned motion vectors need a double-buffered palette, which ADR-5
deliberately does not build, so ticking it as written would leave the roadmap describing a design
that did not ship. Deletes this plan.
*Gate:* the doc's interface table resolves against the shipped headers, and no ticked `ROADMAP`
line describes something the feature did differently.
