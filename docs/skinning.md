# Skinned Meshes — a rig posed on the GPU and drawn from its palette

The runtime half of animation: a rig's bones and clips upload with its mesh, a compute pass poses
every instance into a bone palette each frame, and the mesh shader blends the bind-pose vertices by
it. The other tier, [VAT](docs/vat.md), fetches a *baked* pose from a texture pair instead; the two
share a clip table and a clock and differ only in where a pose comes from.

**This document is a map, not a mirror.** It records the design choices and the contracts that are
not obvious from a signature. The headers linked below are the source of truth.

---

## Design Choices

* **The pose is computed on the GPU, not the CPU.** `assetlib` already evaluates a pose correctly
  (`poseModelTransforms` / `skinningMatrices`), and reusing it would have been far less code — but it
  is per-unit per-frame CPU work, which the roadmap's Guiding Constraints name as the enemy, and it
  does not survive contact with a crowd. `SkinnedPosePass` does it instead: one workgroup per
  instance, one thread per bone.

* **`RenderJob::time` is the only per-frame input.** An instance is spawned with `{clip, phase, rate}`
  and never touched again — the same bargain VAT makes, and deliberately the same three fields
  (`bgl::SkinnedInstanceDesc` beside `bgl::VatInstanceDesc`), so a unit can move between tiers without
  its playback record being rewritten. `rate = 0` holds a pose under any clock.

* **The previous pose is re-evaluated, not remembered.** Motion vectors need last frame's pose. Rather
  than double-buffering the palette, the pose pass writes *two* palettes per instance in one dispatch
  — at `time` and at `prevTime` — and the mesh shader skins both. That is correct on the first frame
  and on an instance spawned mid-frame, where a history buffer holds garbage, and it needs no
  ping-pong.

  **It holds only while time is the sole input to a pose.** A clip switched between frames would
  reproject through the wrong clip. Whoever adds a state machine or a blend owns that.

* **Skinning happens in the mesh shader, not a compute pre-pass.** There is no transient skinned
  vertex buffer: nothing yet needs to *read back* skinned positions (physics, attachments), and until
  something does, a per-instance per-frame allocation buys nothing.

* **The palette stores three rows a bone; the walk composes in `float4x4`.** A skinning matrix's
  fourth row is always `(0,0,0,1)`, and the palette is the largest per-instance allocation in the
  frame — doubled again by the `prevTime` copy — so storing it is waste. The hierarchy walk still
  composes full `float4x4`s, because giving it a packed shape would mean a bespoke affine multiply on
  a convention the rest of the shaders do not use. Only the buffer is packed. That is what sizes
  `cMaxBonesPerRig`: 192 bones of *unpacked* transform is the 12 KiB groupshared budget.

* **Inter-frame blending is nlerp, not slerp.** Clips are resampled to a fixed rate at import, so the
  rotation between adjacent frames is small and nlerp's error with it — and slerp would spend
  transcendentals per bone per instance per frame, twice over.

* **A vertex bound to no bone keeps its bind pose.** Four zero weights are what an exporter writes for
  a vertex it never assigned to a bone. Summing them gives a zero matrix, which would collapse the
  vertex onto the origin, so `SkinMatrix` falls back to identity — matching `assetlib::skinSubmesh`,
  the CPU reference this path is measured against. Weights are otherwise used as authored: the
  importer already normalises anything summing to nonzero.

## The path, end to end

| Stage | Where | What it does |
|---|---|---|
| Import | `assetlib` | A glTF skin becomes `.bskel` (bones, topologically sorted, with inverse binds) + `.banim` (clips resampled to a fixed rate, frame-major local TRS) + `joints0`/`weights0` on the `.bmesh` |
| Acquire | [`AssetManager::AcquireSkinnedMesh`](libs/gamelib/include/gamelib/AssetManager.h) | Reads the three containers, checks the clip set still matches its rig, uploads |
| Upload | [`IScene::AddSkinnedMeshGeom`](libs/bgl/include/bgl/IScene.h) | Bones, clip table and sample pool become scene buffers; per-bone depth is derived here |
| Place | [`ISceneView::CreateSkinnedMeshInstance`](libs/bgl/include/bgl/ISceneView.h) | Writes the playback record and reserves the instance's palette slice |
| Pose | [`SkinnedPosePass`](libs/bgl/src/passes/SkinnedPosePass.h) | One workgroup per instance: sample, blend, walk the hierarchy, multiply by inverse bind |
| Draw | `Forward_SkinnedMesh.slang` | Blends the bind-pose vertex bytes by the palette; position, normal and tangent through one matrix |

## Risky / Non-obvious Contracts

* **The skeleton signature is checked in `gamelib`, not `bgl`.** Computing one needs `assetlib`, which
  `bgl` does not link. `bgl` can only check that the bone *counts* agree — and a reordered rig has the
  same count, so a stale clip set would reach the shader and animate the wrong joints silently.
  `AcquireSkinnedMesh` is the only door that catches it; anything constructing a geom another way
  inherits the gap, which is why `AddSkinnedMeshGeom` documents it.

* **Culling bounds are the bind pose's.** A submesh keeps its cooked bind-pose sphere, because there
  is no all-clips box for a skinned rig the way there is for a bake. A pose that swings a limb outside
  that sphere culls early. Nothing widens it yet.

* **The palette buffer is GPU-written, so it is not a `RangeBuffer`.** That type mirrors its contents
  on the CPU and re-uploads a dirty range, which would overwrite what the pose pass wrote.
  `BonePaletteBuffer` keeps the allocator and the storage as separate pieces for exactly that reason,
  and its growth *discards* — safe only because every live instance is re-posed every frame.

* **The pose pass is attached under the view's namespace, not a cull namespace.** The FrameGraph
  decides a pass is a root by whether it writes an *imported* resource; a name resolved inside a
  per-frustum cull namespace matches no import, and the pass would be culled and never run. A palette
  is per instance, not per frustum, so posing once serves every frustum the view is culled against.

* **One mesh may be live as static, VAT and skinned at once.** Three keyspaces in the `AssetManager`
  (`path#index`, `#vat`, `#skinned`), three uploads — which is what lets the editor compare tiers.

* **Opaque `kPBR` only.** The skinned pipeline has one PSO bucket, the same constraint VAT ships with,
  enforced at every door that binds a material to skinned geometry.
