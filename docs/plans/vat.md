# Vertex Animation Textures — implementation plan

Bakes a rig's clips into textures and draws animated crowds from them: an offline bake evaluates
every skinned vertex at every frame of every clip and packs the results into a position texture and
a normal texture, and a new mesh-shader path fetches them by (vertex, frame) instead of skinning —
no bones on the GPU, no per-unit CPU work, free inter-frame interpolation from the sampler. This is
`ROADMAP.md` **Module 1 → Animation → Vertex Animation Textures**, the tier the battle game's rank
and file live on, which is why it lands before the skinned path.

**This feature depends on PR #109** (`feat/animation-asset-import`), which is open against `master`
at the time of writing. The bake consumes exactly what it cooks: `.bskel` (bones, bind pose, inverse
bind matrices), `.banim` (fixed-rate local-space pose samples), and the `JOINTS_0`/`WEIGHTS_0`
attributes on the `.bmesh`. No task below starts until #109 merges and `feat/vat` rebases onto it.
File references into those containers cite the PR branch and may drift under its review.

This is a *plan*, not a mirror of code. When the work lands, the durable parts belong in a new
`docs/vat.md`, in [asset_standards.md](../asset_standards.md) (the new container and texture
conventions) and [passes.md](../passes.md) (the new forward variant); this file keeps the reasoning.

---

## 1. What the survey found

### 1.1 The inputs #109 provides

- A pose sample is an `assetlib::Transform` (`{vec3, quat, vec3}`, TRS, 40 bytes) in **local
  space** — relative to the bone's parent. Samples are frame-major: bone `b` of frame `f` of a clip
  is `samples[clip.firstSample + f * boneCount + b]`
  ([Animation.h:39-41](../../libs/assetlib_structs/include/assetlib_structs/Animation.h)).
- Bones are topologically sorted (`parent < i`), validated at import, so local→model is one forward
  pass ([Skeleton.h:9-11](../../libs/assetlib_structs/include/assetlib_structs/Skeleton.h)).
- Frames span the **closed** interval `[0, duration]`; a looping clip's last pose duplicates its
  first. Per-clip metadata: `sampleRate`, `duration`, `rootMotion`, `locomotionSpeed`, `loop`.
- `AnimationSet` records the `.bskel` path plus a signature (FNV-1a over bone names and parents,
  **not** the bind pose), and `animationsMatchSkeleton` checks it.
- Skin attributes land as `kJoints0`/`kUint16x4` and `kWeights0`/`kUnorm16x4`, appended after the
  static attributes; weights are renormalized before quantization
  ([bmesh_gltf.cpp:303-353](../../libs/assetlib/src/bmesh_gltf.cpp) on the PR branch).
- **`Bone::inverseBind` is written and never consumed.** There is no per-frame pose evaluation, no
  skinning-matrix composition, and no CPU vertex skinning anywhere — the only hierarchy walk is
  `bindPoseModelTransforms`, bind pose only. T1 exists to close this gap.
- Per-submesh AABBs exist (`Submesh::aabbMin/Max`); **no rig-global, all-clips bounding box exists
  anywhere**. The bake must compute it.
- Runtime is untouched by #109: `gamelib` and `bgl` have no skeleton, clip, or joint concept, and
  the shader-side vertex decoder handles position/normal/uv/tangent only
  ([vertexdecode.slang:14-60](../../libs/bgl/shaders/src/forward/vertexdecode.slang)).

### 1.2 The bake infrastructure that already exists

- `writeKTX2` is format-agnostic: `R16G16B16A16_UNORM` and `R8G8B8A8_UNORM` both have `blockInfo`
  entries and are written verbatim — Basis compression applies only to 8-bit LDR when asked
  ([image_io.cpp:431-532](../../libs/assetlib/src/image_io.cpp)). No mip generator exists for
  16-bit formats; VAT textures are single-mip, so none is needed.
- Content-addressed baked names (`<group>_<16 hex>.ktx2`) come from `bakedMapFileName`
  ([baked_name.cpp:15-25](../../libs/assetlib/src/baked_name.cpp)); the material bake shows the
  richer key shape (inputs + formats folded in,
  [material_bake.cpp:224-253](../../libs/assetlib/src/material_bake.cpp)).
- Staleness is `SourceStamp` (size + mtime) comparison, per the env bake
  ([env_bake.cpp:44-67](../../libs/assetlib/src/env_bake.cpp)).
- Registering a container is a fixed checklist: magic in `magic.h`, extension in
  `container_format.h`, an `*_io` pair over `chunk::Writer`/`Reader`, `AssetType` + extension map +
  `Scan` branch + edge collector in `asset_refs`, a `describe` overload, CLI `sniff`, and — **or
  its textures are swept as garbage** — a branch in `texture_prune`'s mark phase plus a group-name
  predicate for the sweep ([texture_prune.cpp:38-162](../../libs/assetlib/src/texture_prune.cpp)).
- The chunked container (`chunk_io.h`, shared by `.bmesh`/`.bskel`/`.banim` on the PR branch)
  addresses chunks by id; an absent chunk is not an error, which is what makes minor versions
  additive.
- No rigged fixture exists in `assets/` (both `.glb`s carry zero skins). #109's tests synthesize a
  deliberately awkward rigged glTF in code (`SkinnedGltf`, joint order ≠ bone order, distinct
  inverse binds); that is the fixture pattern to reuse.

### 1.3 The draw path being extended

- An instance is two structs: `idl::Mesh` (transform + submesh range, one per placement) and
  `SubmeshInstance` (mesh entry + submesh index + resolved material + pso — the sort key, and
  **hand-mirrored**, not IDL-generated). No time, phase, or clip field exists, and no API mutates
  an instance transform after creation.
- The counting sort buckets on `SubmeshInstance::pso`; `PsoType` is IDL-generated with
  `c_PsoCount = 10`; the forward pass wires one pixel module per bucket in a **positional** table
  (`c_Psos`, [ForwardPass.cpp:148-169](../../libs/bgl/src/passes/ForwardPass.cpp)) and selects the
  mesh source unconditionally (`c_GeomSrc`, `:127,181-182`) — VAT needs that per-row.
- `GeomType` has one live value, `kStaticMesh`, with `//kSkinnedMesh` commented out
  ([GeomType.h:5-11](../../libs/bgl/include/bgl/GeomType.h)); `SubmeshPso` maps
  (geom, material, layer) → `PsoType` ([util.cpp:98-156](../../libs/bgl/src/util/util.cpp)).
- **The motion-vector seam is one variable.** `EvaluateVertex` computes a single `worldPos` and
  feeds it to both `viewProj` and `prevViewProj`
  ([Forward_StaticMesh.slang:30-44](../../libs/bgl/shaders/src/Forward_StaticMesh.slang)); the
  comment at `:32-33` and `ForwardVSOut`'s contract
  ([common.slang:22-28](../../libs/bgl/shaders/src/forward/common.slang)) both say a path with its
  own previous-frame position substitutes it there and touches nothing downstream.
- Texture upload: `IScene::AddTextureAsset(assetlib::ImageData, name)` handles any format in the
  `FromVkFormat` allowlist — `R16G16B16A16_UNORM` and `R8G8B8A8_UNORM` included
  ([vk_format.cpp](../../libs/bgl/src/types/vk_format.cpp)) — and `Scene::CreateSolidTexture` is
  the precedent for an `ImageData` built in memory rather than loaded
  ([Scene.cpp:1177-1191](../../libs/bgl/src/scene/Scene.cpp)). Bindless handles reach shaders
  either through a cbuffer or as an 8-byte `TextureHandle` inside a GPU struct (`Scene.cpp:878`).
- `SampleLevel` exists on `TextureHandle`; the sampler inventory is `kAnisoLinearWrap` and
  `kLinearClamp` ([Scene.cpp:260-267](../../libs/bgl/src/scene/Scene.cpp)) — linear-clamp is
  exactly what VAT sampling needs, so no new sampler.
- Not available today, and **not needed by this plan**: UAV textures (no `RWTexture` anywhere),
  texture arrays through the public path, 3-channel float formats, a point sampler.
- The per-view culling outputs (`CullState`, one per frustum) and the golden/MV test harnesses
  (`MatchesGolden`, `MotionVectors_test.cpp`'s independent-derivation pattern,
  `PerViewCulling_test.cpp`'s hand-built frame graph) are the verification surfaces the tasks gate
  on.

---

## 2. Design decisions

### D1 — A `.bvat` container plus content-addressed textures; bake output, not import output

The bake reads `.bmesh` + `.bskel` + `.banim` and writes `<name>.bvat` beside them plus
`vatpos_<hash>.ktx2` / `vatnrm_<hash>.ktx2` under `Textures/`. The container holds no pixels: clip
rows, global bounds, per-submesh column bases, the skeletal side-channel, texture routes with
stamps, and the paths + signature of what it was baked from.

**Rejected: embedding the pixels in the container.** The whole texture pipeline — ktx2 io, upload,
staleness, prune — exists for files under `Textures/`; an embedded blob re-implements all of it.

**Rejected: extending `.banim`.** Lifetimes differ: a `.banim` is an import artifact re-cut when
the source glTF changes; a `.bvat` is a bake artifact re-cut when the `.banim`, the mesh, *or the
bake parameters* change. Fusing them makes every bake rewrite an import output — the same reason
`.bmaterial` routes and its baked triplet are separate notions.

### D2 — Pose evaluation and CPU skinning live in assetlib

The bake is offline and `assetlib` never links `bgl`, so the evaluator — sample a clip frame,
local→model walk (one forward pass over the topological order), `model × inverseBind`, then skin
each vertex after decoding `kUint16x4`/`kUnorm16x4` — is plain CPU code in `assetlib`, built on
`toMatrix` and GLM. It is also the CPU reference every later GPU test diffs against, which is why
it is its own task with its own tests rather than a private helper of the bake.

### D3 — One 2D texture pair per rig: vertices along U, frames along V, clips stacked

Position: `RGBA16_UNORM`, unorm-packed in **one global AABB over all clips** (blended or
interpolated samples are meaningless across per-clip boxes — `ROADMAP.md:99`). Normal:
`RGBA8_UNORM`, object-space, `xyz * 0.5 + 0.5`. Columns are geometry-local vertex indices — the
same index the mesh shader's vertexMap step yields — offset by a per-submesh column base, so the
runtime fetch is `(columnBase + vertexIdx, clipRow + frame)`. Clips stack along V, each padded
with a duplicated terminal row so fractional-V interpolation never bleeds into the next clip.
Either dimension over 16384 (the D3D12 cap) is refused with an error naming the count that broke
it — vertices for U, total padded frames for V (~9 minutes of clips at 30 Hz) — tiling is a later
feature if a rig ever needs it.

The VAT vertex carries **no tangent**: the pixel stage's degenerate-tangent guard falls back to
the geometric normal, so normal maps are inert on this tier — an accepted cost of the distance it
draws at. Rejected: a third baked tangent texture (another 4–8 B/texel on the dominant term for
detail the tier cannot show) and reusing the bind-pose vertex tangent (wrong the moment a limb
rotates, which reads worse than no normal map).

Memory, worked honestly: a 3,000-vertex rig with 40 s of clips at 30 Hz is 3000 × ~1220 rows ×
(8 + 4) B ≈ 44 MB — and the 50 MB budget (`ROADMAP.md:322`) covers a humanoid *and* an equine rig
together, so that inventory does not fit it. The budget closes by baking from a LOD mesh once the
LOD generator lands (the dominant term is vertex count) and by clip sets shorter than the example;
nothing here precludes either, and the 8-bit normal is already the cheap half. A 16-bit normal
doubles its term for quality the tier cannot show; octahedral `RG16` (same 4 B, better
distribution) is deferred until banding is observed.

**Rejected: one texture per clip.** Bindless makes it feasible, but every clip boundary becomes a
handle swap in the shader, transitions still need padding, and the container must route N textures
per rig instead of two.

**Rejected: `float16`/`float32` positions.** 2–4× the dominant memory term; unorm16 in a box is
~0.1 mm of quantization on a 4 m rig, below anything a crowd tier shows. The box comes with D3's
global-AABB rule for free.

### D4 — Playback state is GPU-resident; time is the only per-frame input

A per-instance record (clip index, phase offset, play rate) is written once at spawn; the shader
derives the frame row from a global `time` carried in `ViewData` alongside a `prevTime` for motion
vectors. Nothing touches instances per frame — "per-unit CPU updates are the enemy"
(`ROADMAP.md:27-28`), and the crowd-variation items (phase offset, rate jitter) fall out of the
spawn-time fields for free.

**Rejected: CPU-updated phase per instance.** O(instances) uploads per frame, and the state
machine that eventually drives clip changes is itself planned as a GPU pass — CPU phase would have
to be un-built.

### D5 — A new geom type and PSO bucket; the linkage rides `idl::Mesh`

`GeomType::kVat`, one new `PsoType::kVatOpaque` bucket (alpha variants when a use case exists), a
new `Forward_Vat.slang` AS/MS pair sharing the existing pixel modules — `ForwardVSOut` is
unchanged, so PBR shading, TAA and the velocity MRT come along untouched. The forward pass's PSO
table gains a per-row mesh source (today `c_GeomSrc` is unconditional).

The linkage: `idl::Mesh` gains an `Entry` to a VAT-state record (null for static meshes — `Entry`
defaults to the null sentinel). The mesh shader already fetches `idl::Mesh` for the transform, so
the fetch chain grows by one hop exactly on the path that needs it, and `SubmeshInstance` — the
hand-mirrored sort key that every cull/compact kernel reads — keeps its 16 bytes. The VAT-state
record points at a per-geom record (texture handles, bounds, clip table range, column bases) in a
new Scene-owned buffer; clip rows live in a parallel GPU buffer so the state machine can index
them later. All new GPU structs go through the IDL.

**Rejected: a field on `SubmeshInstance`.** Grows the struct every compute kernel touches for a
value only the mesh shader wants, and the struct is hand-mirrored — the highest-friction place in
the codebase to grow.

**Rejected: reusing `kStaticMesh` with a runtime branch.** A per-lane branch on every static
vertex in the scene to spare one PSO bucket, in a pipeline whose whole design buckets by PSO.

### D6 — Motion vectors from a second VAT sample, from day one

`prevWorldPos` = the same fetch at `prevTime`'s phase, through the same (immutable) instance
transform, substituted at the documented one-variable seam. Deferring this would ghost every
animated unit under TAA — "not optional since VAT is the majority path" (`ROADMAP.md:83`) — and
the seam was built for exactly this substitution.

### D7 — Sampling: `kLinearClamp` + `SampleLevel(0)`, exact texel centres along U

With U at an exact texel centre the bilinear weight across columns is zero, so one linear sampler
gives point-along-U and lerp-along-V simultaneously — the standard VAT trick, and the reason no
new sampler or point filter is needed. VAT textures are single-mip, so `SampleLevel(0)` and the
clamp addressing close the remaining ways a fetch could wander.

### D8 — Fixtures are synthesized, at both layers

assetlib tests extend #109's `SkinnedGltf` (translate + spin over known bones — closed-form
expected positions). bgl tests build tiny VAT textures procedurally through the public API — bgl
cannot read `.bvat` (layering), and a golden against a 4-vertex, 2-frame texture pins the fetch
math better than any real rig. The end-to-end path (`.gltf` → bake → `.bvat` → scene → pixels) is
gamelib's test, at the seam that actually joins the layers.

### D9 — CLI-only, like the import it extends

`assetlib_cli bakevat` (name final at implementation). The editor's animation checkbox is already
inert under #109 and its import refuses rigged glTFs; wiring editor UI to a runtime that is still
landing would review nothing. Editor flow is a follow-up feature, and because the bake lives behind
the CLI seam, that feature is a caller — the logic is already testable without the shell.

---

## 3. What changes, and what could break

| Where | Change | Risk |
|---|---|---|
| `libs/assetlib` | pose eval + skinning (new files), `bvat_io`, `vat_bake`, CLI subcommand, refs/describe/prune registration | **prune mark-phase must learn `.bvat` in the same PR that writes textures**, or a prune run sweeps live VAT maps |
| `libs/assetlib_structs` | `BVat` POD header | — |
| `libs/bgl/idl` | `VatGeom`, `VatState`, `VatClip`, constants; `Mesh` gains an `Entry`; `PsoType` + `GeomType` gain a value | `idl::Mesh` layout change touches its `static_assert`s; regenerate, never hand-edit |
| `libs/bgl` shaders | `Forward_Vat.slang`; `ViewData` gains `time`/`prevTime` | a cbuffer key missing from any forward binding is `gfatal` at first draw — every pixel module binds `ViewData`, so the field lands in one shared struct |
| `libs/bgl` | scene buffers for VAT records, `AddVatGeom`/`CreateVatMeshInstance` public API, per-row mesh source in the PSO table | `c_Psos` is positional against `PsoType` and its `static_assert` only catches an empty row — a misordered row draws with the wrong pixel shader |
| `libs/gamelib` | `AssetManager` acquires `.bvat` (textures via the existing ktx2 path, then the geom + instance calls) | refcount graph gains texture edges from a geom kind that is not a material — follow the instance→geom→texture discipline in `gamelib/CLAUDE.md` |
| `assets/`, `.gitattributes` | none planned — fixtures are synthesized; if a real rig lands later, `.glb` is already LFS-tracked and `.bvat` (palettes, few MB) should join `.benv` in `.gitattributes` | — |
| `ROADMAP.md`, docs | ticked per task; `docs/vat.md` at the end | — |

Existing behaviour that must not move: every current golden (static path untouched — new PSO
bucket, no shared-shader edits except additive `ViewData` fields), the counting sort (bucket count
is data-driven from `c_PsoCount`), and `.bmesh`/`.bskel`/`.banim` readers (no format bumps needed —
the `.bvat` references them by path).

---

## 4. Tasks

Each is one PR into `feat/vat`, in this order. **T1 starts only after #109 merges to `master` and
`feat/vat` rebases onto it.**

- **T1 — pose evaluation and CPU skinning** (`assetlib`). `poseModelTransforms(skeleton, set,
  clip, frame)`, `skinningMatrices`, and a skinned-vertex evaluator that decodes the quantized
  joint/weight attributes. Gate: a clip frame whose pose equals the bind pose reproduces the source
  vertex positions exactly (inverse-bind cancellation); the awkward two-bone rig animated over
  known TRS channels matches closed-form positions; weights that sum to 1 after renormalization
  stay sum-1 through the quantized decode.
- **T2 — `.bvat` container and the bake** (`assetlib` + CLI). Global AABB over all clips, texture
  write via `writeKTX2`, padding rows, side-channel palettes, stamps, the full registration
  checklist including prune. Gate: container round-trip; unpacking a baked texel matches T1's CPU
  skin within unorm tolerance; the padded terminal row equals the last frame; refs reports the
  five edges (`.bvat` → mesh/skel/anim/two textures) and prune protects both maps; a bake past
  16384 in either dimension — vertices or padded frame rows — refuses with the counted error.
- **T3 — the VAT draw path, fixed frame** (`bgl`). IDL structs, geom/PSO additions, per-row mesh
  source, `Forward_Vat.slang`, public API, procedural VAT upload — phase static, no time yet.
  Gate: golden image of two instances frozen at different frames of a synthesized texture; the
  full existing golden suite unchanged; run under `--gpu-validation`.
- **T4 — playback, interpolation, motion vectors** (`bgl`). `time`/`prevTime` in `ViewData`, GPU
  phase advance, fractional-V lerp, `prevWorldPos` substitution. Gate: a half-phase sample equals
  the CPU lerp of the two poses within tolerance; the motion-vector test derives expected
  displacement independently (the `MotionVectors_test` pattern) for a static camera over an
  animated quad; loop wraparound crosses the padded row without a discontinuity; `--gpu-validation`.
- **T5 — gamelib loads `.bvat`** (`gamelib`). AssetManager acquisition, lifetime edges, the
  end-to-end test: synthesize a rig, bake it through the assetlib API, load the `.bvat`, draw, and
  assert on pixels. Gate: that test, plus delete-order tests (geom before textures, instance
  before geom).
- **T6 — docs**. `docs/vat.md` (bcp-docs shape), the asset-standards and passes updates, roadmap
  ticks reconciled, this plan corrected to what shipped.

Roadmap lines this feature ticks: bake pipeline, global bounding box, per-frame skeletal
side-channel (baked and tested; no GPU consumer yet — say so in the tick), VAT motion vectors,
free inter-frame interpolation. Lines it deliberately leaves: baked transitions and the
constraints/tier-boundary policy lines (authoring policy — transitions *are* ordinary clips, so
nothing ships to tick), masked layering, phase-matched crossfade (needs the pose-distance table),
and everything under the skinned tier and state machine.

---

## 5. Deliberately out of scope

The state machine (VAT units here play one clip from spawn parameters), the skinned tier and the
skinned→VAT LOD swap, transition/layering/crossfade authoring, GPU consumption of the skeletal
side-channel (baked now so every rig cooked from day one carries it — re-baking the whole library
later is the expensive alternative), editor UI and preview, hashed/cutout VAT material variants,
texture tiling past 16384 in either dimension, a baked tangent texture (D3 records why), and
compression of the position texture.
