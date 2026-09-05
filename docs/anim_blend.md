# Animation Blending — weighted slots, crossfades, and 1D blend spaces

A hero instance's pose is a weighted blend of up to four **nodes** of its rig, evaluated on the GPU
from the clock alone. A node is a clip, or a blend space the project authored. This page is the
skinned tier's blending half; [Skinned Meshes](skinning.md) is the tier itself, and
[Passes Overview](passes.md) has the pass that runs it.

## Design Choices

The reasoning that is not obvious from a signature. The headers linked below are the source of truth.

* **Time is the only per-frame input, and that is what the motion vector rests on.** The pose pass
  evaluates a record twice per frame, at `time` and at `prevTime`, and the pair is where a skinned
  motion vector comes from — no history buffer, so an instance spawned mid-frame is right on its
  first. That works only because a record says the same thing at every clock. So a slot holds
  *ramps* rather than values: a weight ramp, a parameter ramp, and a phase measured from the slot's
  own `tRef`. Nothing is written per frame.

  **A rewrite therefore only changes the future.** `ISceneView::SetSkinnedPlayback` rewrites a
  record in place, and the helpers that build one
  ([`anim_blend.h`](libs/gamelib/include/gamelib/anim_blend.h)) keep every live slot's phase, rate
  and `tRef` untouched, so it shows the same frame at every clock. A slot being left is ramped down
  rather than dropped. Kept that way, the record still says at `prevTime` what the frame before this
  one drew, and no temporal epoch moves.

  The one thing that is not exact is a *weight* interrupted mid-ramp: a slot holds a single line, so
  it cannot say one thing before the write and another after. It is wrong at one evaluation,
  `prevTime` on the interrupting frame, and only when a fade interrupts a fade. The pose is exact
  throughout, and the header says why the alternative is worse.

* **Four slots, and the lightest is evicted.** A crossfade between two blend spaces is two nodes,
  and interrupting it is three. Four is the fixed bound the shader's loop needs. A write that wants
  a fifth evicts the lightest slot outright — the one place a pop is accepted, and it takes three
  fades interrupting each other inside one window to reach.

  A slot is twelve 4-byte fields, so a hero record is about 208 B against the two palettes the same
  instance already owns (`2 × 3 × 16 × boneCount`, ~6 KB at 64 bones): under 4 %.

* **One record kind, and the node table is clips first.** `SkinnedState` holds the slots; there is no
  second hero record kind for "blended", because that would be a branch in the pose kernel and
  another in the mesh shader to save bytes on an instance that owns a 6 KB palette. The rig's node
  table is one node per clip, in clip order, then the authored spaces — so a slot naming a clip means
  what it always did, and acquiring with a blend set never moves one.

* **Rotations are nlerp'd, each flipped against the running sum.** Not slerp, which the roadmap
  originally called for: clips are resampled to a fixed rate, so the angle between the poses being
  blended is small enough that the error is invisible, and a slerp per bone per instance per frame is
  transcendentals this tier cannot afford. Flipping against the *running sum* rather than a fixed
  reference is what makes the sign decision continuous in the weights — a crossfade passing 50 %
  flips nothing. This is Unreal's `AccumulateWithShortestRotation`, and
  `assetlib::poseModelTransforms`'s weighted overload is the CPU reference the GPU path is diffed
  against.

* **Blending is local, then the hierarchy is walked.** Never model space: blending two model-space
  transforms of a bone does not produce the pose either clip had, because a parent's error compounds
  down the chain. The blend happens per bone in parent-local space, and the walk runs once on the
  result.

## Blend spaces

A **1D blend space** is an ordered run of clips with the parameter each plays alone at — a walk at
1.5 and a run at 5, say, blended by speed. Its members are *looping* clips, refused otherwise.

* **The members share one normalized phase.** They are clips of different lengths, so a frame number
  means nothing between them: what is shared is the fraction of a cycle, and each member's frame is
  that fraction of its own. Without it a walk and a run blended together would drift in and out of
  step and the feet would slide.

* **The phase advances at the reciprocal of the weighted cycle length**, which is what keeps the feet
  planted as the parameter moves: blending toward a shorter cycle speeds the shared phase up. And
  because the cycle length is itself moving while a parameter ramps, the phase is an *integral*
  rather than a quotient:

  ```
  u(t) = u₀ + rate · ∫ dτ / D(p(τ))     from tRef to t
  ```

  with `D` the weighted cycle in seconds at parameter `p`. It is evaluated in closed form
  ([`blend_space.slang`](libs/bgl_common/shaders/src/lib/anim/blend_space.slang)), split at the
  ramp's ends and at each member the parameter crosses, because `D` is only linear *between* two
  adjacent members and kinks at each one. The approximation `(t − tRef) / D(p(t))` is cheaper by a
  few logarithms and wrong for the ramp's whole duration.

  **The segments must be accumulated in the order the ramp reaches them**, not in table order. A
  falling parameter crosses the members backwards, and a walk in table order fuses two spans of a
  kinked `D` into one term. This shipped once and was caught by review; the gate for it is a falling
  ramp across four members, since a single crossing is visited in the same place either way.

* **Retargeting a parameter rebases the phase first.** Moving the parameter changes the rate the
  phase advances at, so integrating the *new* path from the old reference would land somewhere the
  record never was — a jump on the frame of the write. `RetargetParameter` integrates what the old
  path already covered into `phase` and starts the new one at `now`.

  That is why `BlendSpaceInfo` carries its members: the rebase runs on the CPU and needs their cycle
  lengths. It is a twin of the pass's own integral, and deliberately — a shader cannot be called from
  gamelib, and `bgl_extended` cannot depend on it. Nothing mechanically holds the two in step, so a
  change to either is a change to both.

## The path, end to end

| Step | Where | What |
|---|---|---|
| Author | `.bblend` | Canonical JSON under `Data/Authored/`: the clip set it is authored against, and each space's clips *by name*. [`blend.h`](libs/assetlib/include/assetlib/blend.h) |
| Acquire | `AssetManager::AcquireSkinnedMesh` | Loads it, refuses one naming another `.banim`, resolves each member's name to a clip index, hands `bgl` a `BlendSetDesc` and the caller a table of `spaces` |
| Upload | `IScene::AddRig` | Synthesizes one node per clip, appends the spaces, uploads the node and member tables with the rig |
| Spawn | `ISceneView::CreateSkinnedMeshInstance` | A `SkinnedPlaybackDesc` of four slots, validated against the rig's node count |
| Write | `ISceneView::SetSkinnedPlayback` | The record rewritten in place; `CrossfadeTo` / `RetargetParameter` build the new one |
| Pose | `SkinnedPosePass` | Resolves each slot through the node table, blends what they resolve to, walks the hierarchy |

**Clips are named, never indexed, in anything authored.** An index is a fact about one cook: a
re-import that adds a clip shifts every index after it, silently, which is the failure the skeleton
signature exists to catch one layer down. Resolution happens once, at acquire, where both name tables
are in hand — and it is refused rather than warned, because the caller named the set and a missing
space would be a table quietly short of what was asked for.

**A blend set belongs to the rig, and the rig is keyed on its clip set.** A member names a clip of
one `.banim`, and the pose pass samples one clip set's pool, so a space cannot straddle two. A second
acquire naming a *different* set is refused; one naming none accepts whatever the rig has, since a
caller that asked for no spaces is not wrong to find some. Release the rig to zero to change it.

## Risky / Non-obvious Contracts

* **A slot's `node` is checked against the rig's node count, not its clip count.** They differ by the
  number of authored spaces.
* **A space needs at least two members**, with strictly increasing parameters — two at one parameter
  have no defined weighting between them and the span between them is a divisor. Refused at both
  doors: the document's own validation, and `AddRig`.
* **A member that does not loop is refused.** One phase is shared across the members, and a clip that
  clamps would sit on its last frame while the others cycle.
* **`cMaxPoseClips` is twice `cBlendSlots`.** A space resolves to the two members straddling its
  parameter, so four slots of spaces is eight clips. That struct is held per thread in the pose
  kernel; its register cost has not been measured.
* **A blend-aware culling box does not exist.** The `.banim`'s baked box is the union over every
  frame of every clip, which already covers any blend of them — a blend is a convex combination, and
  the box is convex.
