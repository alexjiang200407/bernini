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

  **It holds only while time is the sole input to a pose.** A clip switched between frames
  reprojects through the wrong clip -- the pose at `prevTime` is evaluated on the clip the instance
  now holds, which nothing drew. A switch is destroy + recreate, so the view's temporal epoch moves
  and the TAA resolve takes that frame whole rather than reprojecting into it (see
  [Temporal Antialiasing](docs/taa.md)); what that buys is one unaccumulated frame instead of a
  ghost. A *blend* between clips has no such edge to hang off, and whoever adds a state machine
  owns it.

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

* **A rigid mesh parented to a joint is bound to that bone at import, not attached at runtime.**
  Eyes, teeth and props are modelled as unskinned meshes parented to a bone, which in every DCC means
  "follow it" — and full weight on that bone is exactly what that means in skinning terms. So the
  importer transforms such a mesh's vertices into the rig's space and writes `joints0`/`weights0` for
  it, and the runtime needs no notion of parenting at all: it draws through the skinned path like any
  other mesh, VAT bakes it, and `posedBounds` measures it. The limit is that the baked transform is
  per mesh, so a mesh instanced by *two* nodes cannot take one — it keeps its bind pose, as before.

* **A bone's transform is the product of every node between it and its bone parent, at every frame.**
  glTF lets ordinary nodes sit between two joints, and a DCC export routinely puts one above the root
  joint — an armature carrying the rig's unit conversion and the clip's travel. Composing that chain
  into the *bind pose* alone is not enough: the joint below usually carries a redundant TRS track of
  its own, and the first frame of any clip then overwrites what the chain contributed. So
  `importAnimations` walks the same chain `importSkin` does, evaluating each node at the sample time.
  This is where a clip's `rootMotion` comes from on such a rig, and it is why a travelling clip is not
  flagged `loop` — its last frame no longer repeats its first.

## The path, end to end

| Stage | Where | What it does |
|---|---|---|
| Import | `assetlib` | A glTF skin becomes `.bskel` (bones, topologically sorted, with inverse binds, each composed from its whole node chain) + `.banim` (clips resampled to a fixed rate, frame-major local TRS, composed the same way) + `joints0`/`weights0` on the `.bmesh` |
| Bound | [`assetlib::posedBounds`](libs/assetlib/include/assetlib/skinning.h) | Skins every vertex at every frame for the box the geom culls by |
| Acquire | [`AssetManager::AcquireSkinnedMesh`](libs/gamelib/include/gamelib/AssetManager.h) | Reads the three containers, checks the clip set still matches its rig, bounds the pose unless given a box, uploads |
| Upload | [`IScene::AddSkinnedMeshGeom`](libs/bgl/include/bgl/IScene.h) | Bones, clip table and sample pool become scene buffers; per-bone depth is derived here |
| Place | [`ISceneView::CreateSkinnedMeshInstance`](libs/bgl/include/bgl/ISceneView.h) | Writes the playback record and reserves the instance's palette slice |
| Pose | [`SkinnedPosePass`](libs/bgl/src/passes/SkinnedPosePass.h) | One workgroup per instance: sample, blend, walk the hierarchy, multiply by inverse bind |
| Draw | `Forward_SkinnedMesh.slang` | Blends the bind-pose vertex bytes by the palette; position, normal and tangent through one matrix |

## In the editor

The Animation panel previews a rig through **either** tier, chosen by a "Preview As" selector
(`AnimationEditorWindow`'s `m_TierSelector`). Both doors hand back a geom and the same clip table, so
the transport, the clip list and the scrubber are the same code either way — which is the point of
`RenderJob::time` being the only per-frame input.

* **Switching tiers re-loads.** They are different uploads (`#vat` against `#skinned`), so the panel
  drops its geometry and acquires again rather than swapping a handle. Not a limitation to route
  around later: a tier is a property of the upload.

* **Switching *to* VAT can refuse.** The VAT tier draws from a bake, and the panel will not make one
  unprompted — seconds of the user's time is a decision, not a load step. `game::VatFreshness` asks
  whether a usable bake exists; anything but `kFresh` stops the load and offers **Bake Now**, and
  declining leaves the panel on the tier it was already showing. A **Bake VAT** button makes the same
  bake deliberately. See [vat.md](docs/vat.md); note this is the *editor's* rule — `AcquireVatMesh`
  still bakes on demand, which is what loading a level wants.

* **The tier decides two things, and they live together.** `PlanAnimationLoad` returns them as one
  `AnimationLoadSteps` so they cannot drift apart: whether the load needs a `.bvat` already baked
  (the skinned tier does not — a bake is seconds of CPU skinning for a texture pair it never samples),
  and whether the posed box is read off that bake or measured. Two fields rather than two tests of the
  source spread through a long function, and the box is the one that punishes drift hardest: it culls
  the geom as well as framing the camera, so taking it from the wrong place hides the mesh rather than
  mis-aiming the view. This is the seam `editor_tests` drives; see below.

  **Whether to offer a bake is not one of them.** Both tiers refuse a material that draws unbaked, so
  the offer follows from `editor::BakeableMaterials` finding one rather than from the source — it was
  once tier-gated, which left the skinned tier reporting exactly the refusal a bake answers without
  offering it.

* **The camera opens at a fixed yaw and elevation.** Nothing in the path knows which way a rig faces:
  authoring conventions disagree on the forward axis, so any fixed yaw shows some rigs a profile —
  the test coyote faces +X where glTF's convention is +Z, and opens side-on. Deriving it from bone
  *names* was built and removed: it worked on that rig only because it is named in English, and
  guessing which bone is a head is the wrong shape for something an author can state. The
  replacement is a bone **tag**, which needs a `.bskel` format change and an answer to what happens to
  a user's tags when the file is re-cooked from its glTF — its own feature, not this one. Until then,
  orbiting once is the cost.

* **Framing uses the posed box, never the bind pose.** See the culling contract below: it is the same
  box and the same reason, and it is why the panel measures it inside its loading screen rather than
  on the render thread.

* **The panel itself is not covered by a test**, and this is a pre-existing gap rather than one the
  skinned tier introduced: `RenderTargetWindow`'s constructor calls `CreateRenderTarget` with a real
  `winId()` and `headless = false`, so no test can construct `AnimationPreviewWindow`. What *is*
  covered is `PlanAnimationLoad` — the tier-dependent decisions lifted clear of the window, which is
  the shape `apps/editor/CLAUDE.md` prescribes for exactly this. The uncovered part is the toggle, and
  it has already shipped one bug that every automated gate passed: a tier switch that acquired the
  geom through the new tier while creating the instance through the old one. A `headless` flag on
  `RenderTargetWindowDesc` is the seam that closes it.

## Risky / Non-obvious Contracts

* **The skeleton signature is checked in `gamelib`, not `bgl`.** Computing one needs `assetlib`, which
  `bgl` does not link. `bgl` can only check that the bone *counts* agree — and a reordered rig has the
  same count, so a stale clip set would reach the shader and animate the wrong joints silently.
  `AcquireSkinnedMesh` is the only door that catches it; anything constructing a geom another way
  inherits the gap, which is why `AddSkinnedMeshGeom` documents it.

* **Culling bounds are the caller's posed box, and `bgl` cannot measure it.** `AddSkinnedMeshGeom`
  takes one and derives every submesh's sphere from it, the same rule VAT follows. The bind pose is
  not a substitute: it stops holding the moment a limb moves, and a clip carrying root motion walks
  the whole rig out of it, so bind-pose culling makes it disappear as soon as it does. Measuring the
  box means skinning a vertex, which means
  decoding a vertex layout — `assetlib`, which `bgl` does not link. `assetlib::posedBounds` is that
  walk (every vertex at every frame, the same one `bakeVat` makes), and `AcquireSkinnedMesh` makes it
  unless the caller hands over a box it already has. The editor does: the walk is seconds on a dense
  rig and the acquire runs on the render thread, so the panel measures it inside its loading screen
  and passes the result down — one box per animated mesh entry, because it is that geom's culling
  volume and a `.bmesh` may hold two rigged meshes. Measuring it at load is a stopgap: it belongs in
  the container, the way `bakeVat` writes `boundsMin`/`boundsMax` into a `.bvat`.

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

* **`kPBR`, anything but blended.** Opaque, cutout and hashed all draw an *opaque shape* — they
  discard rather than blend, so their depth is real and nothing has to be sorted — and each is one row
  of `ForwardPass`'s PSO table against the same skinned geometry shader. Blending is the one that
  would need the depth-sorted list, and there is no skinned variant of it. VAT still ships opaque
  alone. `AcceptsMaterial` (`src/util/util.h`) is the rule, and every door that binds a material to
  animated geometry asks it.
