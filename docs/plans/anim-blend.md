# Authored animation blending on the skinned tier

## Context

A skinned instance plays exactly one clip, fixed at spawn: `SkinnedState {geom, clip, phase, rate,
palette}` ([`SkinnedState.slang:8`](../../libs/bgl/idl/src/SkinnedState.slang)) is written once by
`CreateSkinnedMeshInstance` and never touched again. Idle→walk→run pops, attack→idle pops, and
nothing in the tree can do otherwise — no blend, no mask, no graph container exists in `assetlib`
or `bgl` (a grep for crossfade / blend weight / bone mask / state machine hits only `ROADMAP.md`).

The roadmap's next animation lines all assume this exists: *pose sampling — fixed clip count at
compile time, unused slots weighted to zero* and *cross fade blending* are the two open skinned-tier
lines directly under what #401 shipped (`ROADMAP.md` § Skinned Meshes & Animation); the state
machine, the hit reactions and the crowd's phase jitter sit on top of weighted sampling.
[`InstanceDesc.h:27`](../../libs/bgl/include/bgl/InstanceDesc.h) already reserves the spot: *"the
skinned tier is where a weighted clip list and a bone mask arrive."*

The scope is **non-editor**: an asset, its CPU reference, the GPU evaluation and the gamelib load
seam, proven by the suites. The editor's UI over it is a later feature.

## Decisions

**ADR-1 — Blending is an asset.** A new self-describing container, `.bblend`, holds flat tables: a
list of *nodes*, each a single clip or a 1D blend space over clips, authored against one `.banim`.
A C++ builder API writes it; `gamelib` loads it beside the clip set; `bgl` uploads its tables with
the geom. *Rejected:* a runtime-only API (a weighted clip list and a crossfade call with no file) —
cheapest, but nothing is an asset and the editor would have to invent the format later; and a new
chunk inside `.banim` — the clip set is the importer's product, re-cooked from the glTF, and
authored data inside it is lost on the next import.

**ADR-2 — Three primitives and no more.** A fixed number of weighted slots, a timed crossfade
between nodes, and a 1D blend space. *Rejected for now:* bone-masked layers and additive layers
(the mask is its own skinned-tier roadmap line, the additive flinch sits under hit reactions), and a transition table / state machine (its own
milestone, and the thing that will *drive* these primitives). The slots this feature ships are
the *base set* — their weights normalize to one — and an additive layer, when it comes, is a slot
*kind* applied over that set rather than a member of it, so the normalization rule here does not
have to be reopened.

**ADR-3 — Time stays the sole input to a pose.** The per-instance record holds *ramps* — a weight
ramp per slot for crossfades, a parameter ramp per slot for blend spaces — and the pose pass
evaluates them from the clock. The CPU writes the record only on an event (a transition starts, a
parameter is retargeted) through a new `ISceneView` call, never per frame. This is what keeps the
prevTime motion-vector scheme of `docs/skinning.md` exact: a pose at `prevTime` is still a pure
function of the record and the clock. *Rejected:* CPU-written weights every frame — the per-unit
CPU update the Guiding Constraints name as the enemy, and it would need the palette double-buffered
for motion vectors; and a GPU state-machine interpreter — the roadmap's end state, an order of
magnitude larger, and it needs these tables as its input first.

**ADR-4 — Blend-space members are phase-locked; crossfade sides are not.** Clips in one blend space
share a normalized cycle position so a walk and a run blended 50/50 put the same foot down at the
same moment; the cycle length is the weighted blend of the members' own. A crossfade between two
nodes ramps only the weight and each side keeps its own phase and rate — an *attack* fading into
*idle* has no cycle to share. *Rejected:* independent phase everywhere (walk↔run drifts out of step
within a second), and phase-lock everywhere (forces unrelated clips into one cycle).

**ADR-5 — The n-way blend is weighted nlerp, hemisphere-aligned to the heaviest slot.** Per bone,
in the parent's space: translation and scale by weighted sum, rotation by weighted sum of
quaternions each flipped into the hemisphere of the heaviest slot's, then normalized; the existing
walk runs once on the result. That extends the pairwise nlerp `PoseSkinned.slang` already uses
between frames to four slots, and keeps the roadmap's "local, never model space". *Rejected:*
slerp — exact only pairwise, undefined for four weighted slots, and transcendentals per bone per
instance twice over; and aligning to slot 0 rather than the heaviest, which flips the wrong way
the moment a crossfade passes 50 %. The roadmap line that says "slerp" is amended to say what
shipped and why.

**ADR-6 — `assetlib` owns the CPU reference.** `poseModelTransforms` gains a weighted form over
`(clip, fractional frame, weight)` triples; it is the oracle every GPU gate compares against and
the evaluator a future baker or editor preview can call. *Rejected:* a reference inside `bgl` (the
thing under test) or hand-derived matrices only (how `SkinnedPose_test` works today — fine for
three bones and two frames, not for a blend-space point).

**ADR-7 — No text or CLI authoring route.** The builder API is the authoring surface until the
editor's UI lands; the user chose to keep the feature small. `assetlib_cli describe` prints a
`.bblend` like any container once its `describe` overload exists (`--schema` needs none). *Rejected:* a JSON/TOML
description cooked by an `assetlib_cli` subcommand — a second authoring format the editor's export
would then have to stay compatible with.

The following are decisions the plan makes beneath the grill's, each with what it rejected:

**ADR-8 — The record is `cBlendSlots = 4` node slots.** Each slot is `{node, phase, rate, tRef,
weight ramp w0→w1 over its window, parameter ramp p0→p1 over its own}`; the GPU normalizes the live
weights. Four because a crossfade between two blend spaces is two nodes and an interrupted fade is
three, and the roadmap asks for a fixed count. The cost: a slot is twelve 4-byte fields, 48 B, so
the record grows from 20 B to 200 B per instance — against the two palettes the same instance
already owns (`2 × 3 × 16 × boneCount`, ~6 KB at 64 bones) that is under 4 %, and at ten thousand
skinned instances it is 2 MB. A write that needs a fifth slot evicts the lightest one
outright — the only place a pop is accepted, and it takes three interruptions inside one fade
window to reach. *Rejected:* two fixed sides (`from`, `to`) — an
interrupted crossfade then has to snap, a visible pop; and a variable-length list — not
fixed-size, and the whole point of the slot model is that the shader's loop bound is a constant.

**ADR-9 — The node table is implicit clip nodes first, authored nodes after.** `bgl` synthesizes
one single-clip node per clip of the rig at upload, and a `.bblend`'s nodes follow them. So
`SkinnedInstanceDesc::clip` keeps its meaning (slot 0 plays node `clip`, which is that clip), a rig
with no blend set still plays, and the shader has one kind of slot. *Rejected:* a record that
distinguishes "clip slots" from "node slots" — two code paths for one thing.

**ADR-10 — A write only changes the future.** Every helper that rewrites a record rebases it to
`now`: phases are re-expressed at `tRef = now`, ramps start at or after `now`, and a slot that is
still weighted at `prevTime` is ramped down rather than dropped. The new record then evaluates at
`prevTime` to what the old one would have, which is the property ADR-3 needs. `docs/skinning.md`'s
*"It holds only while time is the sole input to a pose … Whoever adds a state machine or a blend
owns that"* — and the same caveat in `ROADMAP.md`'s skinned motion-vector line — become this rule,
kept by the helpers, rather than a hole. *Rejected:* keeping the previous record for one frame — a history
buffer by another name, wrong for an instance spawned mid-frame.

**ADR-11 — Blend-space phase is `u(t) = u0 + (t − tRef) · rate / D(p(t))`,** with `D` the
weighted cycle length in seconds at the parameter `p(t)`: a member's cycle is
`(frameCount_i − 1) / sampleRate_i` — the `frameCount − 1` intervals `ClipFrames` wraps over since
#404, which the clip's recorded `duration` equals up to the importer's rounding — and a member's
frame is `u · (frameCount_i − 1)`, wrapped. `clip_playback.slang` exports that cycle as a one-line
`ClipCycle(Clip)` that `ClipFrames` itself uses, so the blend path never re-spells it. During a
parameter ramp `D` moves, so the advance rate is slightly off for the ramp's duration —
bounded by ADR-10's rebase, which puts `tRef` at the ramp's start. *Rejected:* the exact integral of
`1/D` across a ramp (log terms, and the ramp can cross member segments) and a stateful advance
(ADR-3). Members must be looping clips; a non-looping member is refused where the set is resolved.

**ADR-12 — Clips are referenced by name, resolved at load.** A `.bblend` names its `.banim` by path
and its clips by name. `gamelib` refuses a set naming another `.banim` than the one acquired;
`bgl`'s `AddSkinnedMeshGeom`, which holds both string pools, resolves names to indices and refuses
an unknown name or a non-looping blend-space member as a `SceneError`, which `AcquireSkinnedMesh`
forwards like every other. *Rejected:* indices — a
re-import that adds a clip shifts every one after it, silently, which is the failure mode the
skeleton signature exists to catch one layer down.

**ADR-13 — The event-write helpers are pure functions over the record.** `bgl` exposes the record
type and a setter, plus free functions that take a record, a clock time and a request and return
the next record (crossfade to a node over a duration; retarget a slot's parameter over a
duration). Pure, so they are tested without a device, and the client owns the copy it mutates.
*Rejected:* a stateful per-instance player object in `gamelib` — wanted eventually by the state
machine, premature now.

## Non-goals

- **Editor**: no panel, no import, no UI. The one editor touch allowed is whatever keeps it
  compiling.
- **The VAT tier.** `VatState`, `VatInstanceDesc` and `Forward_VatMesh.slang` are untouched; the
  shared `idl::Clip` and `clip_playback.slang` are used as they are.
- **Bone masks, additive layers, IK, look-at, root motion, notifies** — separate roadmap lines.
- **A transition table, a state machine, or any per-unit tick** — the primitives only; who calls
  them is the state machine's problem.
- **Any behavioural change to `clip_playback.slang` or `idl::Clip`.** Both are shared with VAT; the
  one edit allowed is factoring the cycle `ClipFrames` already computes into `ClipCycle` (ADR-11),
  which changes no frame.
- **An example app.** The gate is the suites.
- **Rotation compression, per-LOD bone sets, crowd phase jitter.**

## Acceptance

1. **`assetlib_tests`**: a `.bblend` round-trips (builder → bytes → `BlendSet`, field for field);
   a file at the frozen schema still loads (`Frozen_test`); the reference graph sees its edge to the
   `.banim` and rename carries it; the weighted pose reference reproduces `poseModelTransforms` for
   one clip at an integer frame, blends A with A to A at any weights, and matches a hand-derived
   half-angle for two frames at 0.5.
2. **`bgl_tests` palette readback** against the `assetlib` reference: two clips mid-crossfade; a
   blend-space point between two members with phase-locked frames; a weight ramp read at a time
   inside, before and after its window; and the `prevTime` palette after a helper rewrite equals the
   palette the old record gave at that time (ADR-10) — exactly, for a rewrite with no parameter ramp
   live at `prevTime`; ADR-11's drift bounds the rest and is not asserted.
3. **`bgl_tests` pixels**: a 50/50 crossfade of clip A with itself renders identical to A alone.
4. **`bgl_tests` helpers**, device-free: a crossfade rebases and ramps, an interrupted crossfade
   ramps the victim down instead of dropping it, a retarget rebases phase, a fifth node evicts the
   lightest slot.
5. **`gamelib_tests`**: a skinned mesh acquired with a blend set resolves every node; an unknown
   clip name, a non-looping blend-space member, and a set naming another `.banim` are refused, and a
   failed acquire owns nothing.
6. **GPU validation clean** on the pose shader (`--gpu-validation` on D3D12, Metal's shader
   validation on macOS).

## What the survey found

- **The playback record and the pass.** `SkinnedState` is `{geom, clip, phase, rate, palette}`
  ([`SkinnedState.slang:8`](../../libs/bgl/idl/src/SkinnedState.slang)); `PoseSkinned.slang`
  reads it once per workgroup, `SampleBone` nlerps two frames with a hemisphere flip
  ([`PoseSkinned.slang:75`](../../libs/bgl/shaders/src/PoseSkinned.slang)), `PoseInto` walks one
  barrier per depth level and is called twice, at `time` and `prevTime`, into one palette slice
  ([`:165`](../../libs/bgl/shaders/src/PoseSkinned.slang)). The clock is `ViewData::time` /
  `prevTime`, from `RenderJob::time`. The IDL supports fixed arrays
  ([`docs/idlgen.md`](../idlgen.md) § host scalar layout), which is what a slot array needs.
- **The geom.** `SkinnedGeom {bones, samples, clips, boneCount, maxDepth}`; `AttachSkinnedRecords`
  ([`Scene.cpp:790`](../../libs/bgl/src/scene/Scene.cpp)) converts the `AnimationSet` into
  `idl::BoneSample`/`idl::Clip` and uploads through `RangeBuffer`s named in
  [`scene_buffer_names.h`](../../libs/bgl/src/scene/scene_buffer_names.h) and registered in the
  owner's `c_Buffers` table — the rule from #398: a new scene buffer is one `NamedBuffer` entry
  and one name constant, nothing positional. `GetGeomSkinnedInfo` hands the view
  `{record, clipCount, boneCount}`.
- **The instance.** `CreateSkinnedMeshInstance` ([`SceneView.cpp:266`](../../libs/bgl/src/scene/SceneView.cpp))
  range-checks `desc.clip`, allocates two palettes, `m_SkinnedStates.Add`s the record. `EntryBuffer`
  has `Set(slot, value)` ([`EntryBuffer.h:146`](../../libs/bgl/src/scene/EntryBuffer.h)), so a
  rewrite is already a supported operation; `MeshMeta::animState` is the slot to reach it by.
- **`bgl` takes `assetlib_structs` PODs directly** (`AddSkinnedMeshGeom` takes `Skeleton` and
  `AnimationSet`), so a `BlendSet` POD crosses the same seam without a mirror.
- **The clip set.** `AnimationClip {nameOffset, firstSample, frameCount, sampleRate, duration,
  rootMotion, locomotionSpeed, loop}` ([`Animation.h:18`](../../libs/assetlib_structs/include/assetlib_structs/Animation.h));
  names in `AnimationSet::stringPool`; `loop` set iff the last pose equals the first. The container
  is the pattern to copy: a schema from `AssetSchemaBuilder`, chunk ids, a `SkeletonRef` chunk
  ([`banim_io.cpp`](../../libs/assetlib/src/banim_io.cpp)), and the touchpoints a new type has —
  `magic.h`, `AssetType` + extension ([`asset_refs.h`](../../libs/assetlib/include/assetlib/asset_refs.h)),
  a `RefKind` and the graph's scan, `asset_rename.cpp`, `migrate.cpp`, the CLI's `sniff`, and a
  file under `assets/Frozen/`. The editor does not switch on `AssetType` exhaustively.
- **The reference.** `poseModelTransforms(skeleton, animations, clip, frame)` is integer-frame,
  one clip ([`skeleton.h:60`](../../libs/assetlib/include/assetlib/skeleton.h)); `skinningMatrices`
  multiplies through the inverse bind. Both are what the weighted form generalizes.
- **The gates that exist.** `SkinnedPose_test` reads the palette back and compares against
  hand-derived matrices on a three-bone chain with a two-frame clip; `SkinnedRender_test` pins the
  skinned-at-bind-pose == static pixel rule; `SkinnedAcquire_test` covers the signature refusal. All
  three are the files the new gates extend.
- **gamelib.** `AcquireSkinnedMesh(relPath, animationsRelPath, meshIndex, posedBounds)`
  ([`AssetManager.h:235`](../../libs/gamelib/include/gamelib/AssetManager.h)) loads the set and
  its rig through the store, checks the signature, builds `ClipInfo`s, uploads; keyed
  `path#index#skinned`, and a live geom must be re-acquired with the same `.banim`.
- **The loop convention is settled.** #404 made `ClipFrames` wrap a looping clip over
  `frameCount − 1` — the importer counts both ends and the last frame duplicates the first — and
  deleted the spec that described the mismatch. A blend-space cycle is that same quantity, which
  is also `AnimationClip::duration · sampleRate`.

## What changes

| Where | What | What could break |
|---|---|---|
| `assetlib_structs` | `Blend.h`: `BlendSet {animations, nodes, members, stringPool}`, `BlendNode {nameOffset, kind, clipNameOffset, firstMember, memberCount}`, `BlendSpaceMember {clipNameOffset, parameter}`; `magic::c_BBlend` | nothing existing |
| `assetlib` | `bblend_io`: schema, serialize/deserialize/save/load, `loadBlendAnimationsPath` for the ranged scan; `validateBlendSet` (≥2 members, parameters strictly increasing, offsets inside the pool); `AssetType::kBlend` + `.bblend`, `RefKind::kBlendClips`, rename/migrate/sniff/describe; `assets/Frozen/*.bblend`; `AssetStore::LoadBlend`; `poseModelTransforms` weighted overload | `Frozen_test`, the ref-graph tests (a new type is a new row), `migrate` (a new case) |
| `bgl` IDL | `BlendNode`, `BlendSpaceMember`, `BlendSlot`; `SkinnedGeom` gains `nodes`, `members`; `SkinnedState` gains `BlendSlot slots[cBlendSlots]` and loses `clip/phase/rate` | every `SkinnedState` fixture and `SkinnedGeom_test`'s record assertions |
| `bgl` scene | two scene buffers (`scene.blendNodeBuffer`, `scene.blendMemberBuffer`) as `NamedBuffer` entries; `AddSkinnedMeshGeom` takes an optional `BlendSet`, synthesizes clip nodes, resolves names (it has the set's string pool), uploads; `AnimGeomInfo` gains `nodeCount`; `ISceneView::SetSkinnedPlayback` / `GetSkinnedPlayback`, `CreateSkinnedMeshInstance` overload on the full record | the geom rollback path; `SkinnedInstanceDesc` callers unchanged |
| `bgl` public | `SkinnedPlaybackDesc` (the four slots), `CrossfadeTo`, `RetargetParameter` as pure functions | — |
| `bgl` shader | `clip_playback.slang` gains `ClipCycle`; `PoseSkinned.slang`: once per workgroup, resolve every live slot to its `(clip, frames, weight)` samples — ramps, node lookup, `ClipFrames`, member pair, normalized weights are all per instance, as `main` already hoists the `Clip` today; per bone, only the sample loads and the blend, then the existing walk; `clip_playback.slang` untouched | register pressure in the pose kernel; every barrier stays group-uniform |
| `gamelib` | `AcquireSkinnedMesh` gains `blendRelPath` (optional); `SkinnedMesh` gains `nodes` (`BlendNodeInfo {name, kind, parameterMin/Max}`); the `.banim` identity refusal per ADR-12 | the live-geom "same `.banim`" rule extends to "same `.bblend`" |
| `docs` | `docs/asset_schema.md`'s container list gains `.bblend` (task 1); `docs/anim_blend.md` (new, proportionate); `docs/skinning.md`'s *"holds only while time is the sole input"* bullet and `ROADMAP.md`'s skinned motion-vector caveat rewritten to ADR-10's rule; `ROADMAP.md` ticks and the slerp amendment; `docs/passes.md`'s Pose Skinned section (what it samples, its caveat, its **In** list); index in `CLAUDE.md` | — |

## Tasks

Each is one PR into `feat/anim-blend`, in this order.

1. **`assetlib`: the `.bblend` container.** Structs, schema, io, validation, asset-graph
   registration, `Frozen` file, CLI sniff, `docs/asset_schema.md`'s list. *Gate:* `assetlib_tests` — round-trip, `Frozen_test`, the
   reference graph reports `kBlendClips`, rename of a `.banim` rewrites the `.bblend` that names
   it, `migrate` leaves a current file untouched.
2. **`assetlib`: the weighted pose reference.** `poseModelTransforms` over `(clip, frames,
   weight)` triples, fractional frames, nlerp with the hemisphere rule, weights normalized. *Gate:*
   `assetlib_tests` — acceptance 1's three reference assertions.
3. **`bgl`: slots, crossfade and the tables.** IDL structs and the slot record; blend tables
   uploaded with the geom (implicit clip nodes; authored nodes resolved by name); `SetSkinnedPlayback`
   and the full-record create; `CrossfadeTo`; the shader blends clip-node slots under weight ramps.
   So that the PR is whole on its own, `AddSkinnedMeshGeom` refuses a set containing a blend-space
   node until task 4 lifts the refusal. *Gate:* acceptance 2 (crossfade, ramp window, prevTime),
   3, 4 (crossfade helpers), 6.
4. **`bgl`: blend spaces.** The parameter ramp, member resolution, phase lock (ADR-11),
   `RetargetParameter`; the upload refusal from task 3 becomes the looping-member check. *Gate:*
   acceptance 2 (blend-space point, phase-locked frames), 4 (retarget), 6.
5. **`gamelib`: acquire with a blend set.** `AcquireSkinnedMesh`'s optional path, the `.banim`
   identity refusal, `bgl`'s two forwarded, `nodes` on `SkinnedMesh`. *Gate:* acceptance 5.
6. **Docs and roadmap; the plan goes.** `docs/anim_blend.md`, the `skinning.md`, `passes.md` and
   `ROADMAP.md` caveats, the `ROADMAP.md` ticks, then this file deleted — the feature's last PR, so the landing PR carries
   the deletion.
