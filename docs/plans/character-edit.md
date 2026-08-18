# Morph targets for character variation

## Context

One mesh and one clip set is the crowd tier's whole premise, and it is also its whole problem:
`ROADMAP.md:171` names sameness as "the primary visual risk". Everything the roadmap currently
offers against it is skin-deep — a kit index into a texture array, hue jitter, a submesh mask, ±3-4%
uniform scale (`ROADMAP.md:172-183`). None of it changes a silhouette, and none of it gives two
soldiers different faces.

The alternative is authoring a mesh per variant. That works for a hero, who is one unit and can
carry a bespoke asset. It does not work for rank and file: the variants multiply, and every one of
them is a full vertex stream, a full meshlet partition and — on the VAT tier — a full baked texture
pair.

Morph targets are the standard answer, and the inputs are already in the tree. `.bmesh` carries
`joints0`/`weights0` (`docs/asset_standards.md:226-227`), those semantics survive verbatim into the
GPU vertex buffer (`Scene.cpp:189-191`), and `.bvat` already bakes a per-frame skinning palette that
nothing reads (`BVat.h:24-25,82`, `docs/vat.md:68-69`). The one thing missing is a representation
for the deltas: the glTF importer sees morph targets today and drops them, saying so in a comment —
"weights drives morph targets, which this pipeline has no representation for" (`gltf_skin.cpp:421`).

## Decisions

**ADR-1 — the VAT crowd tier is the primary target; the skinned tier follows it.** *Rejected:* the
skinned/hero tier first, on the reasoning that a distinct nose is invisible at crowd distance. A hero
is a unit whose mesh can simply be authored, so morphs buy it the least; the crowd shares one mesh by
construction and has nowhere else for variation to come from. Both tiers ship in this feature —
`ROADMAP.md:270` makes the skinned↔VAT swap a dithered crossfade, so a unit that morphs on one path
and not the other pops at the LOD boundary — but VAT is what the feature is *for*, and it is the tier
whose task lands first.

**ADR-2 — a morph on a VAT vertex is exact, not an approximation.** Linear blend skinning is affine
in the vertex position: `skin(p) = Σᵢ wᵢ Mᵢ p̃`. For a bind position `b` and a bind-space delta `δ`,
which is a direction and so has `w = 0`:

```
skin(b + δ) = Σᵢ wᵢ Mᵢ (b̃ + δ̃) = Σᵢ wᵢ Mᵢ b̃ + Σᵢ wᵢ Rᵢ δ = skin(b) + Σᵢ wᵢ Rᵢ δ
```

where `Rᵢ` is the upper-left 3×3 of `Mᵢ`. The VAT texture supplies `skin(b)`, the baked palette
supplies `Mᵢ`, and `joints0`/`weights0` are already in the vertex stream. So a delta rotated by the
baked palette and added to the fetched position is *the same vertex* a re-bake of the morphed mesh
would have produced. The identity is exact and does not even require normalised weights, since both
sides carry the same `Σᵢ wᵢ`. The one thing it is not is bit-identical to a re-bake: a bake quantises
against the mesh's own AABB, so a morphed source mesh and an unmorphed one land on different unorm
grids, and the runtime addition happens in float after unpacking. *Rejected:* re-baking the texture
pair per face variant — 12 B per (vertex, frame) is
~86 MB for a rig of 8k vertices and ~900 frames, multiplied by the variant count, on what is already
the largest asset in the game. *Also rejected:* adding the bind-space delta unrotated, which costs
nothing and is wrong the moment a bone turns.

**ADR-3 — deltas live in a `.bmorph` sidecar under a new `Morphs/` project directory,** beside
`Skeletons/` and `Animations/` (`Project.h:25-26`). Sparse: a shape stores only the vertices it
touches, as an index plus a position and normal delta. *Rejected:* a section inside `.bmesh`, which
makes every mesh load pay for deltas it may not use and turns re-authoring a face set into a rewrite
of the mesh container. *Also rejected:* one shared file per rig covering body and every garment,
which forces all of them to be cooked together and re-cooked when any one changes.

**ADR-4 — one weight vector per instance, shared by every mesh of the unit; shapes match by name.**
A garment declares shapes under the same names as the body it is worn over, and import validates
that it covers the body's set. A fat body with thin armour is not a state worth being able to
express. Where kit is a submesh of the unit mesh — which is what `ROADMAP.md:177`'s per-instance
submesh mask implies — this costs nothing at all: one mesh, one delta set. *Rejected:* per-mesh
weight vectors. *Also rejected:* deriving a garment's deltas by wrapping it to the deformed body,
which needs a binding solver nobody has written and is fragile on capes and skirts.

**ADR-5 — weights are per-instance and applied in the mesh shader, after vertex decode and before
skinning.** An `Entry<MorphState>` on `idl::Mesh`, exactly the shape `vatState` already uses
(`libs/bgl/idl/src/Mesh.slang:6-13`), pointing at a weight vector. This is the seam
`feat/skinned-mesh`'s ADR-2 already established for skinning. *Rejected:* a compute pre-pass
writing a transient deformed vertex buffer — no transient vertex allocation exists, and
`feat/skinned-mesh` rejected the same thing for skinning for the same reason. *Also rejected:*
deforming once at spawn and registering the result as a new geom, which has zero per-frame cost
and is the only version that could scale without limit, but needs a runtime geometry upload path,
per-face geom lifetime management, and forecloses a weight ever changing.

**ADR-6 — the cook classifies each shape as rigid or blended, and the shader takes the cheap path
when it can.** A shape whose touched vertices are bound overwhelmingly to a single bone — which is
where face features live — stores that one bone index, and the mesh shader loads its matrix once per
meshlet instead of four per vertex. A shape spanning bones, which is what a body-type "fat" morph
is, keeps the full four-influence sum of ADR-2. *Rejected:* blended-only, which is exact everywhere
but reads four palette matrices per morphed vertex per frame — order a gigabyte of palette traffic
per frame at crowd scale. *Also rejected:* rigid-only, which is cheap but cannot express the body
morph that ADR-4's armour case is entirely about.

**ADR-7 — glTF morph targets are the only authoring source.** *Rejected:* authoring shapes in
Bernini's editor. There is no mesh-editing tooling and none is planned; the DCC is where a face is
sculpted.

**ADR-8 — this branch waits for `feat/skinned-mesh` to reach master.** That branch rewrites
`Forward_VatMesh.slang` (its ADR-10 folds `VatClip` into a shared `idl::Clip`) and establishes the
three-row palette packing this feature reuses. *Rejected:* stacking `feat/character-edit` on
`origin/feat/skinned-mesh`, where any revision to the five unmerged commits ripples into every open
PR here.

## Non-goals

- **A slider UI, in the editor or in game.** Weights are set through the runtime API and exercised by
  tests. The API is shaped so a panel is a thin layer over it, but no panel ships here.
- **Weights that animate per frame.** Facial animation and lip sync are parked on the CPU hero tier
  by `ROADMAP.md:169-170`. A weight may change; nothing drives it every frame, and no cost is paid for
  the possibility.
- **Pose-space deformation** — corrective shapes driven by joint angle.
- **Morphs on the static, unrigged mesh path.** Nothing in the game needs a morphing prop.
- **Deriving a garment's shapes from the body** (ADR-4).
- **Per-LOD morph sets**, and any interaction with LOD selection, which does not exist yet.
- **Re-baking `.bvat` per variant** (ADR-2), and any change to the bake pipeline at all.
- **Recomputing normals from the deformed surface.** The stored normal delta is what the shape gets.
- **Delta compression beyond sparse indices and fp16.**

## Acceptance

1. **The exactness gate.** A rig whose source mesh is morphed and *then* baked to VAT agrees, vertex
   for vertex, with the same rig baked unmorphed and morphed at runtime — within the unorm bound of
   the two bakes' AABBs, which differ by construction (ADR-2), so the tolerance is derived from those
   boxes rather than hand-picked. This is the cheapest thing that would prove ADR-2's algebra wrong,
   and it catches a wrong palette row, a wrong rotation and a dropped translation term at once.
2. **`bgl_tests` readback.** A hand-built rig, a known palette and one shape, asserted vertex-for-
   vertex against expected positions — the layer that *localises* a failure of gate 1.
3. **`bgl_tests` golden image** on the skinned path at weights 0 and 1, plus the same rig drawn
   skinned and through VAT at the same time and weights, asserted to agree within the unorm bound.
4. **Armour.** Two meshes sharing a shape name and one weight vector both deform; a garment missing a
   shape the body declares is refused at import, not silently half-applied.
5. **`assetlib_tests`.** `.bmorph` round-trips; a glTF carrying morph targets imports them; the cook
   classifies a single-bone shape rigid and a spanning shape blended.
6. **GPU validation clean** on both morph paths — Metal via `MTL_SHADER_VALIDATION=1`, D3D12 via
   `--gpu-validation`.

## What the survey found

**Every input the design needs is already baked; none of it has a consumer.**

- `.bvat` stores a skinning palette for every real frame — `BVat::palettes`, `glm::mat4`,
  frame-major, `palettes[firstPalette + f*boneCount + b]` (`BVat.h:24-25,82`). `docs/vat.md:68-69`
  says outright that nothing on the GPU reads it. ADR-2 makes this feature its first consumer.
- `joints0`/`weights0` are optional `.bmesh` attributes that arrive together or not at all
  (`docs/asset_standards.md:229-231`), and `Scene.cpp:190-191` copies a submesh's layout wholesale,
  casting semantics straight across — so they are already in the GPU vertex buffer of any rigged
  mesh, VAT included, whether or not anything reads them.
- The importer already sees morph targets and drops them, and says why (`gltf_skin.cpp:421`).
- `idl::Mesh` is `transform` + `RangeWithCount<Submesh>` + `Entry<VatState>`
  (`libs/bgl/idl/src/Mesh.slang:6-13`).
  The optional-per-instance-side-struct shape ADR-5 needs is the one `vatState` already occupies.
- `Project.h:25-26` holds `c_SkeletonsDirectoryName` / `c_AnimationsDirectoryName`; `Morphs/` is one
  more constant beside them, plus an entry in `c_RequiredDirectories`, whose size is written out as
  `10` (`Project.h:32`) and must move with it.
- `assetlib::skinSubmesh` (`skinning.h:38`) is the CPU deformer the VAT bake uses. It is where ADR-2's
  decomposition is provable in ordinary C++, before any of it reaches a shader.
- Geometry arenas are grow-only and never compacted (`docs/geometry_layout.md:119-125`), which is
  why ADR-5 deforms in the shader rather than rewriting vertices.

**What `feat/skinned-mesh` establishes that this feature builds on** (5 commits, unmerged): skinning
applied at vertex-decode time in `Forward_SkinnedMesh.slang` (its ADR-2 — the seam ADR-5 extends), a
`SkinnedPosePass` writing a bone palette per instance per frame, three-row palette packing (its
ADR-7), and `idl::Clip` unified across VAT and skinned (its ADR-10, which rewrites
`Forward_VatMesh.slang`). ADR-8 is why this branch waits.

**Cost, honestly.** The crowd tier is where ADR-5's per-frame sum is a real risk. A morphed head
submesh of ~600 vertices with six active shapes is ~3.6k delta fetches per unit; at 10k units that is
36M fetches per frame per view, and the four-influence path would add ~24M palette matrix loads on
top. ADR-6 is what makes that tractable, and gate 1's companion measurement is a frame time on the
crowd scene with and without morphs. If ADR-6 is not enough, the fallback is the spawn-time bake
rejected in ADR-5, and it is a task-shaped change rather than a redesign.

## What changes

| Area | Change | What could break |
|---|---|---|
| `libs/assetlib_structs/` | `BMorph`, `MorphShape`, sparse delta records | shared with `bgl` by ADR-4 of `feat/skinned-mesh`'s precedent; a hand-mirror here is a silent disagreement |
| `libs/assetlib/` | `.bmorph` container io, glTF morph-target import, dominant-bone classification, `applyMorphs` | classification is a *cook-time* decision baked into the file — get the threshold wrong and a body morph silently becomes rigid and shears |
| `libs/bgl/idl/src/` | `MorphShape`, `MorphState`, `Entry<MorphState>` on `Mesh` | `Mesh` is read by three mesh shaders; adding a field moves offsets under all of them |
| `libs/bgl/include/bgl/` | morph tables on the VAT and skinned geom descs, `ISceneView::SetMorphWeights` | a weight vector sized by the geom's shape count — a mismatched span is a caller error, not an assert |
| `libs/bgl/src/scene/` | delta/shape buffers, per-instance weight storage, palette upload for VAT | the palette is the largest new upload and is per rig, not per instance; VAT gains a buffer it never had |
| `libs/bgl/shaders/src/` | morph sum in `Forward_VatMesh.slang` and `Forward_SkinnedMesh.slang`, shared in `forward/` | the rigid path caches a matrix per shape in groupshared; the meshlet vertex loop is the hottest code in the frame |
| `libs/gamelib/` | `.bmorph` acquire, shape-name matching across body and garments | refcount symmetry with the mesh it belongs to; a morph outliving its mesh |
| `apps/editor/` | `Morphs/` directory, import writes `.bmorph` | the importer's rollback must cover the new file |
| `docs/` | new `docs/morphs.md`; `vat.md`, `asset_standards.md`, `geometry_layout.md`, `ROADMAP.md` updated | — |

## Tasks in order

Bottom-up by layer. Each gate is an assertion, not a build.

**1 — `assetlib`: the `.bmorph` container and glTF morph-target import.** Sparse bind-space deltas
keyed by shape name, written under `Morphs/`, replacing the drop at `gltf_skin.cpp:421`.
*Gate:* round-trip through serialize/deserialize; a fixture glTF carrying morph targets imports every
shape with the vertex indices and deltas the source declares.

**2 — `assetlib`: the dominant-bone classification and the CPU deformer.** `applyMorphs` composing
with `skinSubmesh`, and the cook-time rigid/blended decision (ADR-6).
*Gate:* `skinSubmesh(applyMorphs(mesh, w))` equals `skinSubmesh(mesh)` plus the rotated-delta sum,
asserted numerically — ADR-2's algebra proven in C++ before a shader exists. A single-bone shape
classifies rigid, a spine-spanning one blended.

**3 — `bgl`: morph tables and per-instance weights.** The IDL structs, the shape/delta buffers,
`Entry<MorphState>` on `idl::Mesh`, upload through the VAT and skinned geom paths,
`ISceneView::SetMorphWeights`. Dead scaffolding — nothing draws it yet, and the tests are its only
caller. *Gate:* readback of the uploaded tables against the container they came from.

**4 — `bgl`: VAT uploads its baked palette.** `BVat::palettes` to the GPU, three-row packed, its
first consumer (`docs/vat.md:69`). *Gate:* palette readback asserted bone-for-bone and frame-for-frame
against the container, including a fractional frame's two rows.

**5 — `bgl`: morphs apply in `Forward_VatMesh.slang`.** Both the rigid and blended paths.
*Gate:* acceptance gate 1 — the morph-then-bake versus bake-then-morph golden image — plus gate 2's
readback, plus the crowd frame time with and without morphs recorded in the PR body.

**6 — `bgl`: morphs apply in `Forward_SkinnedMesh.slang`.** *Gate:* acceptance gate 3 — weights 0 and
1, and the same rig agreeing between the skinned and VAT paths.

**7 — `gamelib`: acquiring a `.bmorph` and driving an instance.** Shape-name matching across body and
garment, import-time coverage validation. *Gate:* acceptance gate 4 — two meshes, one weight vector,
both deform; a garment missing a declared shape is refused.

**8 — `docs/morphs.md`, the roadmap, and this plan's deletion.** *Gate:* the doc's usage sketch
compiles as written against the shipped API.
