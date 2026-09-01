# Crowd-tier frame interpolation

The crowd tier reads a fractional frame as the lerp of the two bracketing frames' **composed skin
matrices**, in the vertex stage
([skinned_vertex.slang](../../libs/bgl_extended/shaders/src/lib/forward/skinned_vertex.slang) `:93`). The hero
tier instead nlerps each bone's **local rotation** and then walks the hierarchy, in a compute pass,
once per instance ([pose_walk.slang](../../libs/bgl_extended/shaders/src/lib/anim/pose_walk.slang) `:71`).

The bone-anim-table feature chose the first, deliberately — [docs/skinning.md](../skinning.md)
states the rule and why the two sources differ between frames. Nothing here disputes that choice.
What is recorded is that the option space was searched in one direction only, so the cheaper
alternative was never on the table when it was made.

## The cost nobody priced

Counting `float4` loads per vertex, straight off the two paths — `cInfluencesPerVertex` is 4 and
`cFloat4sPerBone` is 3, and each path evaluates twice, at `time` and at `prevTime`, for the motion
vector:

| | fetches | `float4` loads |
|---|---:|---:|
| Hero — palette already holds the interpolated pose | 4 × 2 | **24** |
| Crowd — two frames lerped at the vertex | 8 × 2 | **48** |

So the crowd path costs **twice** the hero path per vertex. It wins overall by deleting what comes
before the vertex — no hierarchy walk, and no palette writes at all, where the hero tier writes
`2 × boneCount × 48 B` per instance per frame and reads every byte of it back.

That trade is sound at crowd scale. What is unexamined is the lerp itself, because it is expensive
in *both* directions at once, which is unusual:

- it is the 2× above, **and**
- it is the lower-quality interpolation. A linear blend of two rotation matrices is not a rotation:
  it contracts toward the chord, so a limb shortens mid-blend, and because these matrices are
  already composed into model space the error is multiplied by the lever arm down the chain. The
  hero tier's nlerp happens in rotation space, per bone, before composition, so it has no such
  term.

Normally one is paid to buy the other.

## What was rejected, and what was not considered

The alternative that was weighed and rejected is *nlerp of local rotations then a walk, per
vertex* — which is what the pose pass already is, and the reason the hero tier has one. That is the
**quality-up, cost-up** option, and rejecting it is plainly right.

The **quality-down, cost-down** option was never stated: read the *nearest* frame and do not
interpolate. That is 24 loads rather than 48 — parity with the hero tier — and it removes the
matrix-lerp artifact entirely, replacing it with temporal stepping at the clip's authored rate.

Which of those reads worse on a unit at crowd distance is an empirical question, not an obvious
one, and it has not been asked.

## The trigger

**GPU timestamp queries.** The claim above is about the vertex stage specifically, and today
nothing in the tree can attribute a cost to one stage: the RHI has no timestamp query, and
`EndFrame` records a fence and returns rather than syncing
([RenderContext.cpp](../../libs/bgl_extended/src/gfx/RenderContext.cpp)), so wall-clock around a frame
measures CPU command recording. Whole-frame throughput cannot separate a vertex-stage change from
anything else in the frame.

`ROADMAP.md` `:347` carries that work — *GPU timestamp per pass with on-screen breakdown* — and it
is the thing to wait for. A crowd scene that measures vertex-bound is the other trigger, and the
more urgent one.

Until then this is deliberately not measured, rather than measured badly. A whole-frame number
taken on one machine and written down reads later as though it had isolated the stage, which is
the failure this file exists to prevent.

## What to do when it fires

Both variants are small and local to
[skinned_vertex.slang](../../libs/bgl_extended/shaders/src/lib/forward/skinned_vertex.slang): `SkinFromTable`
either lerps the two frames or rounds to the nearer one. Measure the vertex stage on a crowd scene
dense enough to be vertex-bound, at the LOD a crowd unit actually draws at, and look at both the
cost and the motion.

If nearest-frame wins, the rule in [docs/skinning.md](../skinning.md) changes with the code, in the
PR that changes it.
