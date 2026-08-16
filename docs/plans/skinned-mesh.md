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
(`ISceneView.h:50`). *Rejected:* a CPU-driven per-instance time, which would fork the editor's
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

**ADR-7 — the palette is `float3x4` per bone.** The fourth row of a skinning matrix is always
`(0,0,0,1)`. *Rejected:* `float4x4`, which is marginally simpler in the shader but makes the
largest per-instance allocation in the frame 33% bigger, and this one is doubled by ADR-5.

**ADR-8 — inter-frame blending is nlerp with hemisphere correction,** not slerp. Clips are
resampled to 30 Hz, so the rotation between adjacent frames is small and nlerp's error with it.
*Rejected:* slerp, which is exact but spends transcendentals per bone per instance per frame, twice
over under ADR-5.

**ADR-9 — the Animation panel previews skinned or `.bvat`, switchable.** *Rejected:* skinned
replacing VAT preview. Being able to A/B the two is how a bad bake gets caught, and it is the
skinned↔VAT comparison `ROADMAP.md:146` wants, arriving early and nearly free.

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

**1 — `bgl`: the skinned geom and its GPU tables.** The IDL structs, `GeomType::kSkinnedMesh`, the
new `PsoType`, and `IScene::AddSkinnedMeshGeom` uploading bone / clip / sample buffers, with the
per-bone depth derived at upload. Validates the skeleton signature against the `AnimationSet`, that
every submesh carries `joints0` and `weights0`, and the bone-count cap. **The instance half lands
here too** — `ISceneView::SkinnedInstanceDesc` and `CreateSkinnedMeshInstance`, writing an
`EntryBuffer<idl::SkinnedState>` beside `m_VatStates`, and the palette range each instance owns.
Both halves of the seam in one task, because task 2 needs real `SkinnedState` records to dispatch
against and task 3 needs placed instances to render. Nothing draws it yet — dead scaffolding,
justified because the tests call it.
*Gate:* `bgl_tests` builds a small rig, adds the geom, creates an instance, reads the geom tables
and the instance's `SkinnedState` back and asserts both; each documented refusal throws.

**2 — `bgl`: the pose compute pass.** `PoseSkinned.slang` — workgroup per instance, thread per bone
(strided above the group size), clip sampled at `time` with nlerp between the two frames it falls
between, hierarchy walked by depth level with a barrier per level, multiplied by `inverseBind`,
written as `float3x4`. Dispatched twice per instance for `time` and `prevTime` (ADR-5).
`SkinnedPosePass` ordered ahead of `ForwardPass`. Still nothing draws.
*Gate:* **acceptance 2** — palette readback asserted bone-for-bone, at an integral and a fractional
frame, plus a `rate = 0` hold and a looping clip's wrap across its seam.

**3 — `bgl`: draw it.** `Forward_SkinnedMesh.slang`: decode `joints0`/`weights0`, four palette
matrices, linear blend on position, normal and tangent; the `prevTime` palette gives the previous
clip position at the `common.slang:25` seam, so motion vectors fall out. `ForwardPass` wiring for
the new bucket.
*Gate:* **acceptance 1 and 3** — bind-pose-equals-static, the golden image, and the
animating-vs-held velocity assertion. GPU validation run (**acceptance 5**).

**4 — `gamelib`: acquisition.** `AcquireSkinnedMesh(relPath, animationsRelPath, meshIndex)` reading
`.bmesh` + `.bskel` + `.banim` through the `AssetStore` and returning a geom plus a clip table, and
`CreateSkinnedInstance`. No bake and no freshness rule — unlike VAT there is no derived product, the
containers *are* the source, which is most of why this task is small.
*Gate:* `gamelib_tests` acquires a fixture rig, shares the geom on a second acquire, and releases to
zero; a mismatched skeleton signature is refused.

**5 — editor: the Animation panel plays skinned.** A source toggle (skinned / VAT) on the panel;
`AnimationPreviewWindow` acquires through whichever is selected and respawns on a clip change the
way it does today. The transport is untouched (ADR-3).
*Gate:* **acceptance 4**.

**6 — docs, and the plan comes out.** `docs/skinning.md` as the subsystem page — the pose pass, the
palette layout and its lifetime, the `prevTime` constraint from ADR-5, the interface→file table;
`geometry_layout.md` and `passes.md` amended. `ROADMAP.md` ticked — and line 145 **reworded**, not
just checked: it currently says skinned motion vectors need a double-buffered palette, which ADR-5
deliberately does not build, so ticking it as written would leave the roadmap describing a design
that did not ship. Deletes this plan.
*Gate:* the doc's interface table resolves against the shipped headers, and no ticked `ROADMAP`
line describes something the feature did differently.
