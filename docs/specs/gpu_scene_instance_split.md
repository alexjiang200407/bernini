# One record is doing the geom's job and the instance's

`idl::MeshInstance` is a placement, and it is also the only place a geom's identity reaches the GPU.
`WritePlacement` copies the geom's submesh range in **by value**
([SceneView.cpp](../../libs/bgl_extended/src/scene/SceneView.cpp) `:313`), so the record holds the instance's
transform and its playback reference alongside a field that belongs to the thing it was placed from.
There is no geom record on the GPU at all.

That works today because a placement has exactly three fields and one of them is shared. It stops
working at each of the three places the roadmap is heading, and they are the same problem seen from
three directions.

## 1. There is nowhere to put per-instance data

The roadmap's crowd work needs, per unit: an animation phase offset from an ID hash
(*non-negotiable, or a formation reads as one organism*), a `playRate` jitter, a uniform scale and
yaw jitter, an LOD index with hysteresis, a submesh mask, a packed blood/grime `uint32`, a group ID,
and a unit ID. None of it has a home.

Two of them could be pushed into the playback record — `phase` and `rate` are already there — but
that is the wrong seam and it shows: a static placement has no playback record, so a scale jitter
routed through one would be unavailable to exactly the geometry that is not animated. And the group
ID and the blood mask are not animation.

Every comparable engine keeps this separate from both the mesh and the material. Unreal has
`FInstanceSceneData` alongside `FPrimitiveSceneData`, plus a per-instance custom-data float array;
Unity's `BatchRendererGroup` takes arbitrary per-instance property arrays; Godot's multimesh carries
`custom_data` per instance. The renderer being GPU-driven does not change this — it makes it more
acute, because per-unit CPU updates are the thing the roadmap's guiding constraints forbid, so the
variation *has* to be a field the GPU reads.

## 2. Culling repeats itself once per submesh

`CullInstances` runs one thread per `SubmeshInstance`
([CullInstances.slang](../../libs/bgl_extended/shaders/src/programs/culling/CullInstances.slang) `:48`). For
an *n*-submesh placement that is *n* threads, each loading the same `MeshInstance`, each
reconstructing the same three basis vectors to get the same max axis scale, each testing a sphere
against the same six planes. The only thing that differs between them is which submesh's bounding
sphere they read.

There is no mesh-level bound to test first, because there is no mesh-level record to hold one. And
`Meshlet::boundingSphere` — the level *below* — is written by `Scene`
([Scene.cpp](../../libs/bgl_extended/src/scene/Scene.cpp) `:157`, `:1226`) and read by nothing, which
[docs/geometry_layout.md](../geometry_layout.md) already records.

So the hierarchy the data describes is instance → submesh → meshlet, and the hierarchy the culling
walks is one flat level in the middle. Nanite's is instance → cluster, and that two-level shape is
what HZB occlusion needs: a cheap instance test that rejects most work, then a fine test on what
survives. Three of the roadmap's culling lines — HZB occlusion test, per-view culling, density
culling — are all written against an instance-level test that does not exist yet.

## 3. A moving instance has no previous transform

`MeshInstance` carries `transform` and nothing else. There is no transform setter anywhere in `bgl`
— a placement's transform is fixed from `CreateStaticMeshInstance` to deletion — which is why
`static_vertex.slang` projects one world position through both cameras and says so.

The moment the crowd simulation moves a unit, that comment becomes a bug: every moving instance
reports camera-only motion, and TAA reprojects it to where it used to be. The same is true of the
roadmap's *corpses sink 2–4 cm over 1–2 s, instance transform only*.

The field itself is trivial. What is not trivial is that adding it alone doubles the placement
record for a value that is equal to its neighbour on every frame but the ones after a move — which
is exactly why Unreal keeps `PreviousLocalToWorld` in GPUScene beside a dirty-state mechanism rather
than as an unconditional mirror.

## What to build

**Split the record in two, along the line that already exists in the code.**

- **`Geom`** — what `AddStaticMeshGeom` produced and every placement from it shares: the
  `RangeWithCount<Submesh>`, a mesh-level bounding sphere, and (see
  [vertex_layout_per_submesh.md](vertex_layout_per_submesh.md)) whatever else turns out to be the
  geometry's rather than the placement's.
- **`MeshInstance`** — `Entry<Geom>`, the three transform rows, the previous transform, the playback
  reference, the LOD index, and the per-unit data block.

Then:

- `CullInstances` becomes two passes, or one pass over instances feeding a second over the surviving
  instances' submeshes. The instance test reads `Geom`'s sphere once, not *n* times.
- The per-unit fields have an owner that a static placement also has, so scale jitter and a group ID
  do not have to pretend to be animation.
- `prevTransform` lands with the writer that makes it mean something, and the *is this instance
  dirty* question has one record to ask.
- A placement stops carrying a copy of a range it does not own, which also retires the hazard
  [docs/geometry_layout.md](../geometry_layout.md) documents as *a stale instance reads a stale
  default*: today an instance that outlives its geom keeps the range by value and draws whatever
  lands in it next.

The three rows are landed ahead of this, because the packing rule is independent of where the record
lives and it is cheaper to establish once, on three call sites, than on the split's much larger
diff. What is *not* landed ahead of it is `prevTransform`: it has no writer until something can move
a placement, which is this.

## The trigger

**The first per-unit varying field**, whichever the crowd work reaches first — the ID-hashed phase
offset is the roadmap's own *non-negotiable*, so it is likely that one. Adding it to the playback
record instead is the mistake this file exists to prevent: it is cheap, it works, and it puts a
gameplay value inside an animation record where a static placement cannot reach it.

**Or the HZB occlusion test**, which needs an instance-level rejection to be worth building at all.

Not before either. The split costs a rewrite of the cull passes and every buffer the `Scene`/
`SceneView` boundary hands across, and today the flat record is correct — just not extensible.
