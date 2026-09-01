# The PSO bucket is a flattened key that then has to be un-flattened

`PsoType` enumerates a cross product by hand. Fourteen cases today
([PsoType.slang](../../libs/bgl_extended/idl/src/PsoType.slang) `:3`), and the axes are visible in the names:
alpha mode × geometry tier × material type.

The axes are not implicit. `GetPsoFromGeomAndMaterial`
([util.cpp](../../libs/bgl_extended/src/util/util.cpp) `:99`) takes them as three arguments — `GeomType`,
`MaterialType`, `LayerType` — and its whole body is a nested switch that flattens them into one
enumerator. So the key already exists; it is destroyed on the way into the buffer.

That destruction has a price, and it is being paid twice already:

- **`IsTransparentPso` reconstitutes one axis by listing members.** Once in C++
  ([util.cpp](../../libs/bgl_extended/src/util/util.cpp) `:167`) and once in Slang
  ([TransparentDepthKeys.slang](../../libs/bgl_extended/shaders/src/programs/culling/TransparentDepthKeys.slang)
  `:12`), whose comment says outright that it mirrors the other. A new blended PSO that is added to
  one list and not the other draws in the opaque loop *and* takes a slot in the sorted list, or in
  neither. Nothing catches it: both spellings compile.
- **`c_Psos` needs a row per enumerator** ([ForwardPass.cpp](../../libs/bgl_extended/src/passes/ForwardPass.cpp)
  `:100`), guarded by a runtime assert (`:163`) precisely because the array and the enum are two
  hand-maintained lists of the same thing.

## Why this is about to get worse

Every axis the roadmap adds multiplies, not adds:

| Axis | Where | Multiplier |
|---|---|---|
| LOD tier | *LODs* — per-tier compaction into indirect args | ×3–4 |
| View | *Per-view culling* — camera plus each shadow cascade gets its own pass and args | ×4–5 |
| Submesh mask | *Crowd Variation* — "bucketed by mask alongside LOD" | ×2^n |
| Material type | already ×2, and a toon path is on the roadmap | ×3 |

Two of those lines already say *bucketed by* in the roadmap's own words, which is this enum.

The kernels are sized by the enum, so growth is not free either. `PrefixSumInstances` dispatches
`numthreads(kPsoCount, 1, 1)`
([PrefixSumInstances.slang](../../libs/bgl_extended/shaders/src/programs/culling/PrefixSumInstances.slang)
`:17`) — a 14-thread group today, and a scan whose only implementation is a group small enough to
fit one. `HistogramInstances` and `CompactInstances` each hold `kPsoCount` groupshared atomics per
group. At 14 that is all fine. At 200 the scan is wrong-shaped and the groupshared arrays are the
occupancy limit.

## What to build

**Store the axes, and derive the pipeline index from them.** A packed `uint` sort key on
`SubmeshInstance` — bitfields for (view, LOD, alpha mode, tier, material type) in that significance
order — replaces `pso`. Both engines this renderer's shape resembles do exactly this: Unreal's mesh
draw commands carry a sort key whose fields are the passes and states, and Unity's
`BatchDrawCommand` carries a sorting position rather than an enumerated pipeline.

Three things fall out, and they are the reason to do it rather than tidiness:

1. `IsTransparentPso` becomes a mask test on one field. One rule, no list, no mirror — the shader and
   the CPU read the same bits.
2. The counting sort buckets on the key's high bits, and the number of buckets stops being a
   compile-time enum. `PrefixSumInstances` becomes a real multi-group scan, which it will need
   anyway.
3. A pipeline is looked up from the axes rather than named by the product, so a new alpha mode adds
   one row instead of one row per tier per material.

Keep `PsoType` as the *pipeline table index* — `c_Psos` is a legitimate table of real pipeline
objects — but stop shipping it to the GPU as the sort bucket. The GPU should never see the flattened
form, because the GPU is where un-flattening it is expensive and unverifiable.

## The trigger

**The second view.** Per-view culling gives every shadow cascade its own visibility buffer and its
own indirect args, and the view is the highest-significance axis of the key: a cascade draws the same
instance through a different pipeline. Adding that as a fourth dimension of the enum is where the
current shape stops being tenable, and it is the point at which the counting sort has to be rewritten
regardless.

LOD tiers fire it just as surely, and *Per-tier compaction → indirect args* under **LODs** in the
[roadmap](../../ROADMAP.md) is the same sentence with a different noun.

Do not do this for the fourteen cases that exist. The cost of the current shape is entirely in
growth, and it has not grown yet.
