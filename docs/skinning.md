# Skinned Meshes — a rig posed on the GPU, and drawn from a palette or a table

The runtime half of animation: a rig's bones and clips upload once, a pose is computed on the GPU,
and the mesh shader blends the bind-pose vertices by it. Where that pose comes from is the
instance's choice — a palette a compute pass fills for it every frame, or its rig's bone anim table,
posed once and shared by every instance on that rig. The two share a clip table and a clock, and
differ only in where a pose comes from.

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
  and never touched again, whichever source it draws from, so a unit can move between them without
  its playback record being rewritten. `rate = 0` holds a pose under any clock.

* **A posed instance is addressed by its placement, not by its playback record.** A foot planted on
  the ground needs to know where in the world the instance stands, and that is the `MeshInstance` record's
  `transform` — so the pose pass's work list holds *mesh instance indices*, reads the transform
  there, and reaches the playback record through the `playback` entry the placement already carries.
  Copying the transform into `SkinnedState` instead was tried and rejected: it is one fact in two
  records, and nothing would catch the two disagreeing the day a transform becomes mutable. It is
  also the indirection `CullInstances` and `TransparentDepthKeys` already make. The ground itself is
  the scene's (`IScene::SetGround`), one plane until a heightfield exists.

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

* **A rig is a scene object, not part of a geom.** A skeleton and its clips upload once through
  `IScene::AddRig` and every geom skinned to them names the handle. A modular unit — a body cut into
  slots with swappable armour, each slot its own mesh on one skeleton — is several geoms sharing one
  bone table and one sample pool, where a per-geom upload would hold as many copies of the rig as the
  unit has parts. `gamelib` keys the share on the normalized `.banim` path, because a clip set names
  its own skeleton and so the two are one choice. `AssetManager::AcquireRig` is **private** and
  `AcquireSkinnedMesh` keeps its signature: a public one would be a second handle every caller holds
  and releases for a share the manager can make itself. When attachments need a rig by handle, that
  door opens then.

  **The rig outlives its geoms, and `bgl_extended` enforces that rather than trusting it.** `DeleteRig`
  refuses while any geom still names the rig: a geom left pointing at freed bone and sample ranges
  does not misrender, it poses from whatever lands in them next. So the caller deletes geoms first —
  `AssetManager` does it in that order, and reference-counts the rig so the last geom takes it down.

* **A rig's every frame can be posed once instead of per instance, into a bone anim table.**
  `RigFramesPass` runs the same walk over every frame of a rig's clip set and writes the result to
  `Rig.boneAnimTable`; an instance drawing from it then reads a pose rather than computing one, which
  is what takes the crowd tier's per-unit cost to nothing. The walk itself is shared rather than
  reimplemented — [pose_walk.slang](libs/bgl_common/shaders/src/lib/anim/pose_walk.slang) is what both kernels call,
  so the two producers cannot drift. The walk is generic over `IPoseTables` and names no buffer type;
  [PoseTables](libs/bgl_extended/shaders/src/lib/types/PoseTables.slang) is the bindless implementation
  both kernels hand it.

  **It is addressable by (clip, frame, bone) from any consumer**, not private to the mesh shader
  that reads it today: bone `b` of global frame `f` sits at
  `boneAnimTable.GetStart() + (f * boneCount + b) * cFloat4sPerBone`. Nothing else wants it yet.
  Attachments will — a prop following a hand needs that bone's matrix at that frame — and a table
  reachable only from one shader would foreclose them for no saving.

  **It is filled on demand, not at upload.** A rig no crowd instance is ever spawned on never pays
  for one, which matters because the table is the size of the sample pool it is derived from: 68 MiB
  for a 663-bone rig with 2,254 frames, against ~9 MiB for a 60-bone crowd rig with 3,000. Reserving
  it is the cost — a device allocation, ~67 ms at that size, which is the one thing `bgl_extended` opens a
  Tracy zone for ([docs/profiling.md](profiling.md)). The posing is a dispatch of one workgroup per
  frame and does not register against a frame at that scale.

  **A growth of the arena re-queues every rig holding a table.** The storage is a
  `BonePaletteBuffer`, which discards on growth — safe for the per-view palette, which is rewritten
  every frame, and not for a table written once. Offsets survive a growth, so what a re-queue costs
  is the posing, not a re-allocation.

* **The crowd tier is the standard one, with two deliberate departures.** Posing a rig's every
  frame once and having instances read it is Unreal's AnimToTexture in its *Bone* mode — the City
  Sample crowd — and Unity's Animation Instancing. Both were built for the same problem, and the
  reason to follow them is the same one that motivated this feature: what a crowd instance reads is
  a **bone matrix**, so several slot meshes can share one pose and a unit's armour becomes a
  wardrobe rather than a re-bake.

  Where we differ, and why:

  - **The table is a GPU buffer in the palette's three-rows-a-bone layout, not a texture.** Unreal
    stores it as a texture because its consumer is a material graph. Ours is
    [skinned_vertex.slang](libs/bgl_extended/shaders/src/lib/forward/skinned_vertex.slang), which already reads a
    palette in exactly that layout, and every read is an exact row — there is nothing for a sampler
    to do.
  - **It is filled on the GPU at load, not baked offline.** Unreal and Unity bake because they have
    no hierarchy walk available at load time; we do (`PoseRigFrames` runs the same `pose_walk` the
    per-instance pass runs). Baking would mean a container, a staleness rule, bake-on-demand,
    `pack`'s re-bake and an editor bake dialog — roughly 1,500 lines, all of it caching what one
    dispatch regenerates — plus a CPU pose evaluator obliged to agree with the GPU one. It cost
    exactly that machinery to retire VAT, which was that design.

  The cost of the second is real and worth stating: there is no CPU-readable palette on disk.
  `assetlib::poseModelTransforms` serves anything that needs a pose on the CPU.

* **The pose source is a property of the instance, not of the geom.** `SkinnedInstanceDesc::source`
  chooses: `kPerInstance` gets a palette slice `SkinnedPosePass` fills every frame — the hero tier,
  and the only source a per-unit blend, mask or IK can ever vary — while `kBoneAnimTable` reads the
  rig's table and allocates nothing. One geom serves both, so two instances of one mesh may draw
  from different sources in the same frame, and a unit changes tier by respawning rather than by
  being re-uploaded.

  **The source is the kind of playback record the placement holds**, and nowhere else. A hero
  instance gets an `idl::SkinnedState`, a crowd one an `idl::SkinnedTableState` — the same
  `{rig, clip, phase, rate}` and no palette, because the pose it draws is the rig's and belongs to no
  instance. The mesh shader reads the arena's `RecordHeader` to know which, which is what that header
  is for: the alternative, one record kind with the palette left null and the branch reading the
  hole, is a second way of saying what the arena already says. What still turns on the palette itself
  is `SkinnedPosePass`'s dense list, and only because a palette is what that pass writes into.

  **Between two frames the two sources differ, by design.** The pose pass nlerps local rotations and
  then walks; the table lerps the two frames' finished skin matrices, because skinning is linear in
  the matrices and one blend per vertex replaces one per attribute. On a whole frame they agree
  exactly, and on a rotation between frames they do not. What that trade costs was never priced
  against the cheaper option — one frame's matrices fetched and interpolated the way the pose pass
  does it — and there is no GPU timestamp query to price it with.

  **What the table buys is the pose pass, and the number depends on the rig.** 2,000 instances of a
  64-bone rig six levels deep drew in 1.06 ms/frame against 1.22 posed per instance — about 13%,
  debug Metal. Three dimensions move that, and two of the three are the rig rather than the mesh:
  the walk the table removes costs one barrier-synced level per *depth* (the same 64 bones as a
  chain rather than a tree costs ten times the levels, and measured ≈33% instead of 13%), it costs
  `instances × bones`, and the per-vertex fetches the table adds cost whatever is drawn. A two-bone
  rig shows no win at all.

  **Read the 13% as an upper bound.** The fixture's four-vertex strip fetches the same two bones at
  the same two frames on every instance, so the table's reads are as cache-hot as they ever get; a
  real crowd — colder reads, heavier units — pays more for them than this measures.
  `[.posetiming]` in `SkinnedRender_test` is what measures it, and it reports the depth for this
  reason.

  **The structural argument is the stronger one, and it is not in that number.** The pose pass is
  dispatched over every skinned instance the view holds, with no visibility or LOD test between —
  `RebuildPosedList` walks the mesh buffer, and one workgroup runs per entry — so the hero tier pays
  `instances × bones` for a unit that is off screen, behind the camera or one pixel wide. The
  table's cost is per vertex *drawn*. A crowd at LOD distance is where the two diverge hardest, and
  the fixture, with every instance on screen and four vertices each, is where they diverge least.

* **Skinning happens in the mesh shader, not a compute pre-pass.** There is no transient skinned
  vertex buffer: nothing yet needs to *read back* skinned positions (physics, attachments), and until
  something does, a per-instance per-frame allocation buys nothing.

* **The palette stores three rows a bone; the walk composes in `float4x4`.** A skinning matrix's
  fourth row is always `(0,0,0,1)`, and the palette is the largest per-instance allocation in the
  frame — doubled again by the `prevTime` copy — so storing it is waste. The walk still multiplies
  full `float4x4`s, reconstructing the fourth row and re-packing around each `mul()`; what moved is
  where the intermediate *lives*. It composes in the palette slot each bone already owns, because a
  model transform is affine too and fits the same three rows. The walk therefore needs no storage of
  its own, and nothing bounds a rig's bone count — where the groupshared array it used to hold capped
  one at 192.

* **Inter-frame blending is nlerp, not slerp.** Clips are resampled to a fixed rate at import, so the
  rotation between adjacent frames is small and nlerp's error with it — and slerp would spend
  transcendentals per bone per instance per frame, twice over.

* **A vertex bound to no bone keeps its bind pose.** Four zero weights are what an exporter writes for
  a vertex it never assigned to a bone. Summing them gives a zero matrix, which would collapse the
  vertex onto the origin, so `SkinMatrix` falls back to identity — matching `assetlib::skinSubmesh`,
  the CPU reference this path is measured against. Weights are otherwise used as authored: the
  importer already normalises anything summing to nonzero.

* **A clip is grounded at cook, not planted at runtime.** Clips arrive authored against whatever
  ground plane their author worked on, and the two are not the same plane: of the 29 rigs measured,
  28 float or sink — the test Coyote's `Run` cycle never comes within 0.151 of `y = 0` and peaks at
  0.536 (a third of its own height), `Sleep` sits 0.073 under it, `Walk` crosses it by ±0.06 each
  step. So `groundClips` moves each clip's root track down by the lowest point its mesh reaches over
  that clip, and records the move in `AnimationClip::groundOffset`.

  **Foot IK is not this, and would not fix it.** The standard solve — Unreal's Foot Placement node —
  preserves a foot's *animated* height relative to the character root and adds the ground height
  beneath it, so on a flat plane at the root's own height it corrects by exactly zero. Grounding the
  clip is the prior fix, and it is the one every rig needs; planting feet on uneven ground is the
  separate feature above it, and the roadmap's line for it still stands.

  **The reference is the lowest frame, not the first.** Unity's clip importer offers *Root Transform
  Position (Y) → Based Upon (at Start)*, which references the feet at frame 0; the Coyote's `Run`
  opens at 0.494, near the top of its gait, so that rule would drive its planted phase 0.343
  underground. The whole-clip minimum is right on 13 of its 15 clips. Where it is wrong it is wrong
  because the lowest frame is not the standing one — `Land`'s is its impact compression — and the
  `.bimport`'s `clipFloor` names the height that clip actually stands at instead. Every cook honours
  it — the import writers read the document standing beside the source, and a re-import carries it
  forward rather than overwriting it with what the cook measured. It is a *parameter*, so editing one
  also stales the `.banim` and the next load re-cooks it; `assetlib_cli describe` prints the floor
  each clip was authored at, which is where the number to author comes from.

  **The measurement is exact but walks neither every frame nor every vertex.** A bone's box from
  `posedBounds` holds every vertex weighted to it, and a skinned position is a convex combination of
  its bones' products, so the lowest box corner is a lower bound on the lowest vertex. That one
  inequality is applied at two granularities. Per frame: the cheap sweep orders them, the most
  promising is skinned, and every frame bounded at or above that result is dropped unvisited. Per
  vertex: a frame that survives skins only the vertices whose own bones reach below the best floor so
  far — on a rig standing still, the feet.

  The second is not an optimisation of the first, it is what makes it hold. A box is 1.09–1.51×
  loose, so on `cha800_00`'s 2254 frames — three of its five clips are a character standing still —
  the frame prune leaves roughly a **quarter** of them, not a handful. Skinning all 170k vertices of
  each was 95% of a 79 s grounding pass in a debug build, and gating by bone brings that pass to
  14 s — the same floors, to the last digit. What remains is the pose walk, which grounding and the
  posed-bounds bake below still make separately. `exactPosedBounds`' six minutes is what neither
  prune buys you.

  **Grounding runs before every box**, because a box measured first describes a rig standing
  somewhere the runtime never draws it — and that box culls the geom as well as framing the editor's
  camera. It runs against *every* mesh in the project that skins to the rig, not one: a body and a
  separately imported cloak are drawn as one character and stand on whichever hangs lower, and a
  clips-only import brings no mesh of its own at all. Each of those is read through `LoadRegenMesh`
  rather than off disk, because the re-export that stales a clip set stales its geometry with it, and
  a floor measured off the stale copy moves the rig to where that geometry used to be.

* **A rigid mesh parented to a joint is bound to that bone at import, not attached at runtime.**
  Eyes, teeth and props are modelled as unskinned meshes parented to a bone, which in every DCC means
  "follow it" — and full weight on that bone is exactly what that means in skinning terms. So the
  importer transforms such a mesh's vertices into the rig's space and writes `joints0`/`weights0` for
  it, and the runtime needs no notion of parenting at all: it draws through the skinned path like any
  other mesh, and `posedBounds` measures it. The limit is that the baked transform is
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
| Ground | [`assetlib::groundClips`](libs/assetlib/include/assetlib/skinning.h) | At cook, before the boxes: each clip is moved so the lowest point its mesh reaches over it rests on `y = 0` |
| Bound | [`assetlib::bakePosedBounds`](libs/assetlib/include/assetlib/skinning.h) | At import: sweeps a box per bone through every frame (`posedBounds`) and stores the result in the `.banim`, keyed by a content signature so a re-authored source falls back to measuring |
| Acquire | [`AssetManager::AcquireSkinnedMesh`](libs/gamelib/include/gamelib/AssetManager.h) | Reads the three containers, checks the clip set still matches its rig, culls by the baked box (`findPosedBounds`) — measuring only a pairing the cook never saw — uploads |
| Upload the rig | [`IScene::AddRig`](libs/bgl/include/bgl/IScene.h) | Bones, clip table and sample pool become scene buffers; per-bone depth is derived here; a rig whose caller supplies a `FootPlantDesc` carries its leg chains and per-frame plant weights alongside them. Once per clip set, not once per mesh |
| Upload the mesh | [`IScene::AddSkinnedMeshGeom`](libs/bgl/include/bgl/IScene.h) | The bind-pose submeshes, exactly as the static path uploads them, against a rig handle |
| Place | [`ISceneView::CreateSkinnedMeshInstance`](libs/bgl/include/bgl/ISceneView.h) | Writes the playback record and reserves the instance's palette slice |
| Pose | [`SkinnedPosePass`](libs/bgl_extended/src/passes/SkinnedPosePass.h) | One workgroup per instance: sample, blend, walk the hierarchy, plant whatever feet the rig authored, multiply by inverse bind |
| Draw | `lib/forward/skinned_vertex.slang`, blend in [`lib/anim/skinning.slang`](libs/bgl_common/shaders/src/lib/anim/skinning.slang) | `ResolveSkinnedPose` settles the pose source once per mesh-shader group — one group being one instance — and `SkinnedVertex` blends the bind-pose vertex bytes by it; position, normal and tangent through one matrix. Entered from `programs/forward/SkinnedMesh.slang`, or from `programs/forward/AnyMesh.slang` where a draw mixes tiers |

## In the editor

The Animation panel previews a rig through **either** pose source, chosen by a "Preview As"
selector (`AnimationEditorWindow`'s `m_TierSelector`). It names `bgl::PoseSource` directly rather
than mirroring it into an editor enum, so the two entries are the two values and there is no mapping
to keep in agreement beyond the one below.

* **Switching sources respawns; it does not re-load.** Both draw one upload, so the panel destroys
  its animated instances and creates them again against the same geoms — the same destroy-and-recreate
  a clip switch does, there being no mutate-instance API by design. This is what the pose source
  being a property of the instance buys — a unit moves between sources without being uploaded twice.

* **The selector's mapping is pinned by a test, and has to be.** The two sources draw the same picture
  at a whole frame — that is the crowd tier working — so a selector wired to the wrong source, or to
  nothing, looks exactly like a correct one. `AnimationEditorWindow::TierSourceAt` /
  `TierIndexFor` are what `editor_tests` drives. An index the combo cannot deliver answers with the
  hero tier, which is what an unset `SkinnedInstanceDesc::source` gives.

* **Whether to offer a material bake has nothing to do with the source.** Both refuse a material that
  draws unbaked, so the offer follows from `editor::BakeableMaterials` finding one — it was once
  tier-gated, which left the skinned tier reporting exactly the refusal a bake answers without
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
  box and the same reason. The panel reads it off the `.banim`'s bake, and only a pairing the cook
  never measured is walked — inside its loading screen rather than on the render thread.

* **The panel itself is not covered by a test**, and this is a pre-existing gap: `RenderTargetWindow`'s
  constructor calls `CreateRenderTarget` with a real `winId()` and `headless = false`, so no test can
  construct `AnimationPreviewWindow`. What *is* covered is the selector's mapping, lifted clear of the
  window as `apps/editor/CLAUDE.md` prescribes. The class of bug this once shipped — a tier switch that
  acquired the geom through one tier and created the instance through the other — is now unreachable
  rather than untested: there is one acquire and one geom, and the source is a field on the spawn.

## Foot planting

A rig that authored legs has each one solved onto the scene's ground plane before its palette is
folded through the inverse binds. Nothing else in the frame changes: the plant is a per-instance
compute step inside `PoseSkinned.slang`, and the forward shaders never learn it happened, because
the palette was already the whole interface between the two.

**It runs in the one window where a bone is in model space.** The shared walk seeds local
transforms, composes the hierarchy a depth level at a time, then multiplies each slot by its inverse
bind. Between the walk and that multiply — and nowhere else in the frame — every slot holds a bone's
model transform, which is what a solve against a world plane needs. That is why `pose_walk` exposes
the two halves (`PoseModelSpace`, `FoldInverseBind`) as well as the `PoseInto` that runs both:
`PoseSkinned` calls `PlantFeet` between them, and a caller with nothing to do in that window never
sees the seam.

**Only the hero tier plants.** `PoseRigFrames` fills a rig's bone anim table once and every crowd
instance of that rig reads it, so there is no placement to plant against — one table cannot hold
several instances standing on different ground. A crowd instance draws the unplanted pose, which is
why the crowd path calls `PoseInto` and the per-instance path does not.

**A leg is four bone indices and a sole plane** (`SkinnedLegChain`), uploaded with the rig through
`AddRig`'s `FootPlantDesc` and refused unless `hip → knee → ankle → toe` is a direct parent chain:
the solve rewrites those slots and carries their descendants rigidly, so a twist bone between two
links would
be left holding a pose the joints above it no longer agree with. `bgl` resolves no names and measures
no soles — both need assetlib — so whoever loaded the containers fills the desc in. It rides the rig
and not a geom on it: a leg is bone indices into the skeleton and a weight per frame of the clip set,
so two meshes on one rig plant the same feet.

**The plane crosses into model space as a row vector**, `mul(worldPlane, modelToWorld)`, which needs
no inverse. The instance's inverse is still built, once per group, to carry world down into model
space: a planted foot is dropped *vertically* onto the plane rather than projected along its normal,
because the closest point on a slope is not the point the animation was authored over.

**The target is the ankle joint, not the sole.** The joint is aimed at the ground under it lifted by
the foot's own height along the ground normal. Deriving it from where the sole currently sits does
not converge — the solve rotates the ankle along with the shin, so that target is one the solve then
moves. Taken with the tilt below, the sole lands exactly on the plane whatever the chain did, since
the only part of the ankle-to-sole offset with any height is the part along that normal.

**Then the ankle turns onto the ground**, bringing the sole normal onto the plane's, about the ankle
joint and clamped to `cSoleClampRadians`. About the joint rather than about the sole because the
joint is where the chain the solve just satisfied ends; turning the foot under it would put the shin
back out of length. The sole then lifts off the plane by `1 - cos t` of its distance to the joint,
which at the clamp is under two millimetres on a foot of any size we cook.

**Everything is scaled by a plant weight** — one byte per leg per frame, packed four to a uint,
baked by the cook and sampled at the same fractional frame the pose is. A weight of zero is exactly
the pose the rig would have had. A weight rather than a flag because a foot that snapped between the
two states would pop, which is what the cook's ramp at each end of a planted run exists to remove.

**Descendants ride the nearest solved ancestor's delta.** Each bone walks up its parent chain until
it finds one, which is bounded below by the shallowest solved depth — above that no ancestor can be
solved, so a spine or a tail leaves the walk after a step or two. The deltas live in a groupshared
array sized by `cMaxLegsPerRig`, not by the bone count: a bone-count-sized array is what once capped
a rig at 192, and `AddRig` refuses more legs than the array holds.

**The solve is limb-neutral; only the policy around it is a leg's.** `SolveTwoBone` takes a
root/mid/tip chain and a target, and `CarrySolvedDescendants` takes whatever bones some thread
marked solved — neither knows what a leg is. What foot planting owns is where the target comes from
(the ground under the ankle) and what weights it (the baked plant weight). A second kind of solve in
this window — a hand reaching for a prop — supplies its own two and calls the same pair, rather than
copying the barrier discipline and the two exclusions in the fixup, which are the delicate part.
Two things it would still need designing for: `Rig` carries a field per chain kind today and
would want one range with a kind tag at the second, and it has to run *after* the pelvis drop, since
that moves the whole rig out from under any target resolved before it.

**One thread solves one leg.** A chain is serial — hip before knee before ankle — so a second lane
has nothing of its own to do. The group then carries the descendants together. All three barriers sit
outside the per-leg and per-bone branches, and the whole stage is skipped on a group-uniform test of
`rig.legs`, so a rig without an avatar pays one branch and no barrier.

**The pelvis is not lowered when a leg cannot reach.** The chain is held just inside full extension
and the foot simply stops short. Lowering the root by the largest deficit across a rig's legs is the
next stage.

## Risky / Non-obvious Contracts

* **The skeleton signature is checked in `gamelib`, not `bgl_extended`.** Computing one needs `assetlib`, which
  `bgl_extended` does not link. `bgl_extended` can only check that the bone *counts* agree — and a reordered rig has the
  same count, so a stale clip set or mesh would reach the shader and animate the wrong joints
  silently. `AcquireSkinnedMesh` is the only door that catches it; anything constructing a geom
  another way inherits the gap, which is why `AddSkinnedMeshGeom` documents it.

* **Both halves of a skinned draw record their rig, and both are checked.** A clip set carries
  `AnimationSet::skeletonSignature` and a mesh carries `BMesh::skeletonSignature`; the pair is
  refused by `animationsMatchSkeleton` and `meshMatchesSkeleton`
  ([skinning.h](libs/assetlib/include/assetlib/skinning.h)) wherever a mesh and a rig are first
  brought together. Each container's cache key holds only its *own* bake token, so re-cooking a
  `.bskel` leaves a `.bmesh` current by design — the signature is what turns that from a mesh
  posed by the wrong bones into a refusal naming it.

* **One rig serves any number of sources.** An import binds a `.bskel` whose signature matches
  rather than writing its own, and the `.bimport` records which one
  ([import_document.h](libs/assetlib/include/assetlib/import_document.h)) — nothing derives it, so a
  second `.glb` skinned to a humanoid already in the project reaches the same file its clips do. Two
  rigs of one signature are refused as ambiguous rather than picked between, because directory order
  would otherwise decide which one a clip set names.

* **Culling bounds are the caller's posed box, and `bgl_extended` cannot measure it.** `AddSkinnedMeshGeom`
  takes one and derives every submesh's sphere from it. The bind pose is
  not a substitute: it stops holding the moment a limb moves, and a clip carrying root motion walks
  the whole rig out of it, so bind-pose culling makes it disappear as soon as it does. Measuring the
  box means reading a vertex's influences, which means
  decoding a vertex layout — `assetlib`, which `bgl_extended` does not link. `assetlib::posedBounds` is that
  walk, and it is paid at **import**:
  `bakePosedBounds` stores the result in the `.banim` — one box per rigged mesh entry, because it is that geom's
  culling volume and a `.bmesh` may hold two rigged meshes. Each box is keyed by a signature over
  the vertex data and the inverse binds (`posedBoundsSignature`), so a source re-authored since the
  bake simply stops matching. `AcquireSkinnedMesh` reads the bake (`findPosedBounds`) and walks only
  a pairing the cook never measured — a caller that cannot block still hands over its own box. A
  project imported before the boxes existed is retrofitted with
  `assetlib_cli bakebounds -p <project>`.

* **The posed box is bounded per bone, not per vertex, and is conservative.** Each bone carries one
  box over the vertices it has weight on, in its own frame (the inverse bind is folded in once), and
  a pose sweeps that box instead of the vertices inside it. A skinned position is a convex
  combination of its bones' products, so the union holds it; an axis-aligned box swept by a rotation
  gains slack the vertices do not, so the box is loose rather than tight. Measured against
  `exactPosedBounds` — which does skin every vertex at every frame, and exists only as that
  reference — the test project's rigs come out 1.09–1.51x by volume and 1.00–1.29x on any one axis.
  It buys the cost: `cha800_00.glb` (663 bones, 27 mesh entries, 170k vertices, 2254 frames) bakes
  in 3.5 s where the exact walk needs about six minutes, both in a debug build. Bounding each bone
  by the *whole* bind-pose box would over-estimate ~3x and is what makes the per-bone approach look
  unusable; the difference is that a bone here is credited only with the vertices it moves. All
  entries share one walk of the clip set, so a rig drawn as 27 meshes evaluates each pose once —
  what now dominates the bake is that pose walk (2.4 s of the 3.5 s), not the boxes.

  **Every figure here is a debug build, and the gap to a release one is wide enough to mislead**:
  the same bake is 131 ms optimised, 27x cheaper. Read them against each other, never against a
  release stage line. They are also not the cook's largest number — grounding the clips is, and it
  is measured against them above.

  **The clip set is the largest thing a rig ships, and it is stored uncompressed.** Frame-major local
  TRS is `boneCount * frameCount` 40-byte `Transform`s with nothing elided: 59.7 MB on the rig above,
  ~780 ms to deserialize in a debug build. That is a load cost paid per clip set, not per instance,
  and it is the number to beat before any of the sharing above matters.

  Reading the bake back answers for every mesh entry in one call, for the same reason: the signature
  a box is matched on (`posedBoundsSignature`) describes the whole mesh, so asking per entry hashes
  the vertex pool once per entry — 740 ms against 29 ms on the rig above.

* **The palette buffer is GPU-written, so it is not a `RangeBuffer`.** That type mirrors its contents
  on the CPU and re-uploads a dirty range, which would overwrite what the pose pass wrote.
  `BonePaletteBuffer` keeps the allocator and the storage as separate pieces for exactly that reason,
  and its growth *discards* — safe only because every live instance is re-posed every frame.

* **The pose pass is attached under the view's namespace, not a cull namespace.** The FrameGraph
  decides a pass is a root by whether it writes an *imported* resource; a name resolved inside a
  per-frustum cull namespace matches no import, and the pass would be culled and never run. A palette
  is per instance, not per frustum, so posing once serves every frustum the view is culled against.

* **One mesh may be live as static and skinned at once.** Two keyspaces in the `AssetManager`
  (`path#index`, `#skinned`), two uploads. The rig is keyed separately again, on the `.banim` alone,
  so both uploads of one mesh still share a single skeleton with every *other* mesh cooked against
  it.

* **`kPBR`, any layer.** Opaque, cutout and hashed all draw an *opaque shape* — they discard rather
  than blend, so their depth is real and nothing has to be sorted — and each is one row of
  `ForwardPass`'s PSO table against the same skinned geometry shader. Blending is the one that needs
  the depth-sorted list, which holds every tier at once and draws them through `programs.forward.AnyMesh`
  (see [Passes](docs/passes.md)), so a blended rig sorts against blended static geometry rather than
  after it. What is refused is a *material type*: no unlit and no loose variant of the skinned
  pipeline exists, and a material's kind is read from its own record rather than stamped by the
  geometry stage. `AcceptsMaterial`
  (`src/util/util.h`) is the rule, and every door that binds a material to animated geometry asks
  it.
