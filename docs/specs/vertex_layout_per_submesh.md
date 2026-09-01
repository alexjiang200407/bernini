# Every submesh carries a full vertex layout, and every vertex decodes through it

`Submesh` embeds a `VertexLayout` by value
([Submesh.slang](../../libs/bgl_extended/idl/src/Submesh.slang) `:9`). A `VertexLayout` is eight
`VertexAttribute`s of three `uint`s each, plus `attributeCount` and `stride`
([VertexLayout.slang](../../libs/bgl_extended/idl/src/VertexLayout.slang)) — **104 bytes**, in a struct whose
total is 144. Seventy-two percent of every submesh record is a decode rule, and the same rule, byte
for byte, on nearly every submesh in a project: `AddProceduralGeom` emits the full 48-byte
pos/normal/uv/tangent layout for every primitive, and an imported mesh emits one of a handful of
packed subsets.

The storage is the smaller half of the problem. `DecodeVertex`
([vertexdecode.slang](../../libs/bgl_extended/shaders/src/lib/forward/vertexdecode.slang) `:34`) loops over
`attributeCount` and switches on `semantic` and on `format` — **per vertex**, in the mesh shader,
for a rule that is constant for the whole submesh and very nearly constant for the whole scene.
Every lane re-runs the interpreter to discover, again, that position is a `float32x3` at offset 0.

## What the other engines do

None of them interprets a layout per vertex, and each avoids it a different way:

- **Unreal** — vertex factories. The layout is a shader permutation: the decode is straight-line
  code specialised at compile time, and a mesh that wants a different layout gets a different
  factory.
- **Unity** — a fixed stream set. The layout is fixed by the pipeline, and a mesh that does not
  match is re-laid-out at import.
- **Godot** — a format bitmask (`ARRAY_FORMAT_*`) that selects a shader variant. The decode branches
  are resolved by the variant, not at runtime.

The common shape is: **a small closed set of formats, resolved before the vertex stage.** That
generalises to a GPU-driven renderer without difficulty, because the resolution point is the
pipeline, and this renderer already has one pipeline per (tier, material, alpha mode) — see
[pso_sort_key.md](pso_sort_key.md).

## What to build

Two changes, separable, in this order:

1. **Deduplicate the storage.** `Submesh.layout` becomes `Entry<VertexLayout>` into a small layout
   buffer, and `Scene` interns layouts as geometry is added. `Submesh` drops from 144 to 44 bytes,
   which matters because the cull pass loads a whole `Submesh` per instance
   ([CullInstances.slang](../../libs/bgl_extended/shaders/src/programs/culling/CullInstances.slang) `:64`)
   to read sixteen bytes of bounding sphere out of it. This is mechanical and does not touch the
   decode.

2. **Retire the per-vertex interpreter.** Name the closed set of layouts the cook can actually emit,
   give each a compile-time decode, and select between them the way the pipeline is already
   selected. A layout that is not in the set is a cook error, not a runtime branch — which is the
   same bargain `AddSkinnedMeshGeom` already makes when it refuses a mesh the skinned path could not
   draw.

Do not attempt (2) before the set is actually closed. Today `assetlib` emits whatever the source
primitive carried, and the layout is what makes that safe; closing the set is a cook-side decision
about what a `.bmesh` may contain, and it belongs in
[docs/asset_standards.md](../asset_standards.md) before it belongs in a shader.

## The trigger

**Whichever comes first:**

- **A measured vertex-stage cost.** The interpreter is per-vertex work in the stage that runs most
  often, and no timestamp query exists to price it ([ROADMAP.md](../../ROADMAP.md) § Profiling) — the
  same blocker [crowd_frame_interpolation.md](crowd_frame_interpolation.md) is queued behind. Do not
  guess: the loop is eight iterations of scalar work against four `float4` bone fetches, and which
  dominates is not obvious.
- **Meshlet-level culling.** Reading a `Meshlet`'s bounding sphere per meshlet means loading
  `Submesh` far more often than once per instance, at which point the 104 bytes are on the hot path
  and part (1) pays for itself on its own.
- **A second vertex format in the crowd path.** Per-LOD bone sets (roadmap, *Animation*) change the
  joint/weight attributes per LOD, which is a second layout on the same geometry — and the first
  time the layout is a *variant* rather than an accident of the source file.
