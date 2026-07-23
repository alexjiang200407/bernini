# GPU culling — implementation plan

Implements the **Culling** items from the roadmap: Frustum Culling, Two-pass HZB occlusion culling,
and Culling verification. Geometry Cluster Culling is **dropped** — see the scope note below.

This is a *plan*, not a mirror of code. It records the staging, the verification gate at each
checkpoint, the design decisions and their reasons, and how this work coexists with the in-flight
multi-context migration ([spec](../specs/multi_context_bgl.md), [plan](multi_context_bgl_plan.md)).
When the work lands, the durable parts move to a new `docs/culling.md`, plus updates to
[geometry_layout.md](../geometry_layout.md) and [passes.md](../passes.md).

**The load-bearing property of the whole feature: culling is image-invariant.** A correct culling
system removes only work that could never have contributed a pixel. Every existing golden-image
test is therefore a culling regression test for free, and every checkpoint's gate includes
"goldens unchanged" — a golden diff after any culling change is a bug, never a regen.

> **Scope update — cluster culling dropped.** Meshlet-level (cluster) culling is dropped. As scoped
> it was frustum-only (no cooked normal cone, so no backface cull), which pays off only for objects
> large enough to *straddle* the frustum edge — near-nothing for the on-screen, human-sized skinned
> meshes that dominate this engine's cost. Skinned meshes cannot use static meshlet bounds anyway
> (bind-pose spheres are wrong once vertices deform), and the one large mesh class, terrain, is
> better segmented into tiles that instance-level frustum + HZB culling handle. The real levers for
> the skinned-human bottleneck are **instance HZB** (hidden characters) and **LOD** (distant ones),
> not per-meshlet work. Cluster culling stays a possible future optimization for large *static*
> geometry — but only once a per-meshlet normal cone is cooked, so it earns the backface win rather
> than just straddle. This removes checkpoint **C2** and the meshlet half of **C4** (C4b); the
> instance-level path (C1, done; C3; C4a) is unchanged.

---

## 1. What the survey found

Where the pipeline already is, against what the four features need:

* **The bucketing pipeline is the natural host.** `CompactInstancesPass`
  ([CompactInstancesPass.cpp](../../libs/bgl/src/passes/CompactInstancesPass.cpp)) already runs a
  GPU counting sort over every instance each frame — histogram by `PsoType`, prefix sum, scatter
  into `scene.compactedInstances` — and emits per-bucket `idl::DispatchArgs` that `ForwardPass`
  consumes via `DispatchMeshIndirect`. Instance culling is a filter on exactly this data flow;
  no new pipeline shape is needed, only a visibility input to the existing kernels.
* **Meshlet bounding spheres exist and are dead weight today.** `idl::Meshlet` carries
  `boundingCenter`/`boundingRadius`, populated at import
  ([bmesh_gltf.cpp](../../libs/assetlib/src/bmesh_gltf.cpp)) and by `Scene`'s procedural path —
  and no shader reads them. They were cooked for cluster culling, which this plan drops; they stay
  dead weight until a future revival (see the scope note).
* **There are no instance- or submesh-level bounds on the GPU.** `idl::Submesh` has ranges and
  counts only; `SubmeshInstance` is `{mesh, submeshIndex, material, pso}`. The cooked
  `assetlib::Submesh` does carry a per-submesh AABB — it just never reached the renderer.
  Frustum culling needs a per-submesh local bound; the world bound is derived in-shader from
  `Mesh.transform`.
* **The amplification shader dispatches blind.** `ASBase`
  ([common.slang](../../libs/bgl/shaders/src/forward/common.slang)) runs one group per instance
  and ends in `DispatchMesh(submesh.GetMeshletCount(), 1, 1, payload)` — every meshlet, every
  frame. It stays that way: cluster culling (which would have compacted survivors here) is dropped.
  It remains the hook if cluster culling is ever revived.
* **No frustum data reaches the GPU.** `Camera` stores view/projection only; `DrawData` carries
  `viewProj` and `cameraPos`; there is no plane extraction, no near/far, no previous-frame
  anything, on either side of the bus.
* **HZB is greenfield, and the gap is in the RHI, not the passes.** `TextureUsageFlag` has no UAV
  bit, the depth buffer is created `kDepthStencil`-only with no SRV and is exposed to passes as a
  `DsvHandle` only, and no mip-downsample utility exists. `TextureBarrierDesc` does already
  support per-mip barriers, so the framegraph side is ready.
* **Depth is standard-Z** (`glm::perspective`, D24S8, far plane at depth 1), so the HZB reduce is
  a **max** — an object is occluded when its nearest depth is farther than the farthest occluder
  depth over its footprint. If reversed-Z ever lands, the reduce flips to min; keep it a single
  named constant in the HZB shader.
* **The verification machinery already fits.** The compute-readback test pattern
  (`CompactInstances_test.cpp`, `TransparentSort_test.cpp`), the golden harness
  (`GoldenImage.h`), and the GPU-assert path (`dbg_assert`/`DebugBuffer`,
  [gfx_debug.md](../gfx_debug.md)) cover the three kinds of claim culling makes: exact survivor
  counts, unchanged images, and in-shader invariants.

---

## 2. Design decisions

### One level: instance culling in compute

Culling runs at the **instance** granularity, in compute. A `CullInstances` kernel is sub-pass 0 of
`CompactInstancesPass`: one thread per instance computes the world-space bounding sphere
(`Mesh.transform` × submesh local sphere, radius scaled by the largest column norm) and writes a
per-instance **visibility word** to a per-view buffer. The existing histogram, compact, and
`TransparentDepthKeys` kernels read the word and skip invisible instances. One extra dispatch and
one word of bandwidth per instance, in exchange for: the test runs once (not re-derived in each
consumer), the transparent path is culled by the same decision, and the word has room for what the
roadmap sends next — an LOD index, an occlusion phase bit (§C4), an animation-tick tag.

Meshlet-level (cluster) culling in the amplification shader is **dropped** (see the scope note at
the top). `ASBase` keeps dispatching every meshlet; the geometry-layout contract and the mesh-shader
path are untouched. A useful side effect: culling now lives entirely in portable compute, which a
future WebGPU backend (no mesh shaders) inherits directly, with no separate cull path to port.

### Bounds come from what is already cooked

`idl::Submesh` gains a local bounding sphere. Implementation found the source better than the plan
assumed: `assetlib::Submesh` already carries a tight per-submesh AABB (`aabbMin`/`aabbMax`),
computed from positions at import — so `Scene::AddStaticMesh` derives the sphere by circumscribing
the cooked AABB, and `AddProceduralGeom` folds the same AABB over its generated vertices. No
assetlib schema change, no re-cook, and no meshlet-union approximation needed.

### Per-view state lives on `SceneView`

The HZB, the visibility-word buffer, and (in C4) the visibility history are all functions of one
view: sized to its target, meaningful only for its camera. They live on `SceneView` (or a small
per-view culling-state object it owns), not on `Scene`. This is a multi-context decision as much
as a rendering one — see §5.

### Culling is parameterized by a view, not by "the camera"

The cull uniforms are a `CullView` struct from day one: view-proj, six frustum planes, near/far,
and (from C4) an HZB handle and dimensions. Today exactly one is built per `Draw`, from
`RenderJob.camera`. Cascaded shadow maps will build N of them per frame from light projections,
and the kernels must not care which. Costing this now is a struct definition; retrofitting it
later is a signature change through every kernel.

The `CullView` reaches the kernel as a one-element buffer the CPU rewrites each draw, not as
constant-buffer scalars. A Slang compute cbuffer that also holds bindless `.Handle` buffers drops
its plain fields from reflection, so the planes would silently vanish beside the instance/mesh
handles the cull kernel needs. Routing the whole struct through a buffer sidesteps that and is the
same shape C4 wants anyway: an HZB texture handle is buffer data, not a scalar.

### No async compute

All culling work records on the context's single `"main"` queue. The framegraph's multi-queue
support records but does not fence across queues, and the multi-context migration has just made
queues per-context property (S2b). An async-compute cull is a plausible future optimization; it is
not part of this plan, and nothing here forecloses it.

---

## 3. Data and API changes

| Change | Where | Notes |
|---|---|---|
| `Submesh` gains `float3 boundingCenter; float boundingRadius` | `libs/bgl/idl/src/Submesh.slang` + `just idl` | Offsets shift; the `static_assert`s in the generated header and any hand-written offset assumptions must follow |
| `CullView` struct (viewProj, planes[6]; near/far and HZB fields join in C4 with their first consumer) | IDL | Shared C++/Slang; built CPU-side per draw |
| Per-instance visibility word buffer | `SceneView` | Sized with the instance buffer; a word, not a bit (§2) |
| CPU frustum-plane extraction + sphere test | `core` or `bgl` math util | Gribb–Hartmann from the combined matrix; unit-testable without a device |
| Culling stats counters (tested / frustum-culled / occlusion-culled) | small `ComputeBuffer`, read back via the existing debug-readback ring | Debug builds and tests; the overlay in C5 reads the same buffer |
| `TextureUsageFlag::kUnorderedAccess`, depth SRV, per-mip UAVs | RHI (`Texture.h`, `ResourceManager`/`Texture_d3d12`) | C3; the one place D3D12 code is touched |
| New GPU-assert codes (`kCullSurvivorOverflow`, …) | `libs/bgl/idl/src/ErrorCode.slang` | `dbg_assert` in every cull kernel under `BERNINI_GPU_DEBUG` |

---

## 4. Checkpoints

Each checkpoint builds, passes its gate, and is independently revertable. Shader edits require a
full `just build` (per-target builds run stale DXIL), and any checkpoint touching shaders,
barriers, or descriptors gates on `just run bgl_tests -- --gpu-validation`.

### C0 — Bounds and frustum groundwork

No rendering change; everything verifiable on the CPU or by buffer readback.

* Frustum-plane extraction and CPU sphere-vs-frustum test, unit-tested against hand-computable
  cases (axis-aligned frusta, points/spheres straddling each plane, degenerate transforms).
* `Submesh` bounds in the IDL, populated from the cooked AABB (`AddStaticMesh`) and a vertex
  fold (`AddProceduralGeom`).
* `CullView` struct and the CPU code that fills it, snapshot-tested.

**Gate:** `just test` green; goldens byte-identical (nothing reads the new fields yet); a
`bgl_tests` readback case asserts the submesh buffer holds the expected bounding spheres for a
crafted two-submesh mesh.

### C1 — Instance frustum culling

* `CullInstances` kernel as sub-pass 0 of `CompactInstancesPass`, writing the visibility word;
  histogram, compact, and `TransparentDepthKeys` consume it. The sub-pass follows the existing
  intra-pass rules — separate framegraph sub-passes so barriers derive, the documented UAV
  exception only where the pass already uses it.
* `CullView` plumbed through `DrawData` → `ForwardData`/cull uniforms.
* Stats counters written; `dbg_assert(survivors <= total)` in the kernel.

**Verification:**
* Readback test: instances placed at known positions against a known camera; assert the exact
  per-PSO counts in `compactDispatchArgs` and the visibility words — the
  `CompactInstances_test.cpp` pattern, extended.
* A transparent instance outside the frustum is absent from the sorted partition.
* Every golden test unchanged (all-visible scenes must render identically).
* Off-by-one sweep: a sphere overlapping each plane by a hair survives. (Not *exact* tangency —
  plane normalization is inexact, so a distance of exactly -radius floats to either side; C0's
  frustum tests learned this.)

**Gate:** `just test` green, goldens unchanged, `--gpu-validation` clean, stats show the expected
cull count in the crafted scene.

### C2 — Meshlet (cluster) culling — dropped

Dropped from the plan (see the scope note at the top). As scoped this was frustum-only meshlet
culling, whose only payoff is objects large enough to straddle the frustum edge; the engine's hot
geometry (human-sized skinned meshes) does not straddle and cannot use static meshlet bounds, and
its one large class (terrain) is better segmented into instance-culled tiles. A future revival is
worth it only with a cooked per-meshlet normal cone, so it earns the backface win — at which point
it is its own plan, hosted in `ASBase` exactly where §1 identified the hook. `idl::Meshlet` still
carries its `boundingCenter`/`boundingRadius`; they stay unused until then (or until a meshlet-level
HZB is ever wanted).

### C3 — RHI groundwork: depth as a texture, UAV mips, HZB build

Pure infrastructure; the frame's output is untouched because nothing consumes the HZB yet.

* `TextureUsageFlag::kUnorderedAccess` and the UAV descriptor path; depth SRV
  (`R24_UNORM_X8_TYPELESS` view over D24S8); the depth buffer exposed to the framegraph as a
  readable texture, not only a `DsvHandle`.
* `HzbBuildPass`: a mip-chain max-reduce, one dispatch per mip with per-mip barriers
  (`TextureBarrierDesc` already supports `baseMipLevel`/`mipCount`). A single-pass SPD-style
  builder is a later optimization; per-mip passes are simpler and the framegraph derives the
  barriers.
* The HZB texture is per-view state on `SceneView`, (re)created on resize.

**Verification:** a `bgl_tests` case renders a known depth arrangement (or uploads synthetic
depth), builds the HZB, reads back every mip, and asserts each texel is the max of its footprint —
including the odd-dimension edge texels, the classic HZB bug. RHI-level tests for UAV texture
create/write/readback.

**Gate:** `just test` green, goldens unchanged, `--gpu-validation` clean.

### C4 — Two-pass HZB occlusion culling (instance level)

The restructuring checkpoint. Instance-level only — meshlet-level HZB (the former C4b) is dropped
with cluster culling; see the scope note at the top.

* `SceneView` gains a persistent visibility history (one word per instance slot).
* Phase 1: cull (frustum + "visible last frame") → bucket → draw; `HzbBuildPass` runs on the
  resulting depth. Phase 2: cull all instances against frustum + fresh HZB; instances that pass
  now but were not drawn in phase 1 are bucketed and drawn; the history is rewritten from phase-2
  results. `CompactInstancesPass` and `ForwardPass` execute twice with a phase flag — the pass
  objects already rebuild their framegraph nodes per frame, so this is more passes, not new
  machinery.
* Correct by construction on disocclusion and camera cuts: phase 2 always tests against a real
  current-frame HZB, so the worst case is one expensive frame, never a missing object. An empty
  history (first frame, epoch change) degrades to "draw everything in phase 2".
* History invalidation: the instance buffer is packed, so slots shift on removal. Any scene/view
  epoch change clears the history to all-visible. Coarse but correct; per-slot generation tags are
  the refinement if epoch churn shows up in the stats.
* Transparents are HZB-tested in phase 2 only and never contribute to the HZB.
* The sphere/AABB-to-screen-rect projection and mip-selection are a shared shader helper — the same
  function later serves HZB particle collision.

**Verification:**
* Occluder readback test: a wall in front of N instances; phase-2 survivor count ≈ 0 for the
  hidden set, exact counts asserted.
* A two-frame test pinning the handoff: frame 1 populates history and HZB, frame 2's phase 1
  draws exactly last frame's visible set.
* Camera-cut test: teleport the camera, assert nothing visible is missing (golden compare against
  a cold render of the same view).
* All goldens unchanged; editor sanity pass for popping (manual, until C5's freeze mode makes it
  inspectable).

**Gate:** `just test` green, goldens unchanged, `--gpu-validation` clean, occlusion stats nonzero
in the crafted scene, editor frame time not regressed (two-phase overhead must pay for itself in
the target scene; record numbers in the PR).

### C5 — Verification tooling (the verification roadmap item)

What makes the culling passes trustworthy over time, not just at landing:

* **Freeze-culling debug toggle**: freeze the `CullView` (and HZB) while the camera flies free —
  the standard way to *see* what culling decided. Editor keybind + per-draw debug flag.
* **Stats overlay** in the editor: tested/culled per level per phase, from the C1 stats buffer.
* **Culling-off toggle** (per-draw flag forcing all-visible) and a randomized equivalence test:
  for randomized scenes and cameras, render with culling on and off and assert MSE ≈ 0 via the
  golden harness's compare. This is the property test for the image-invariance claim, and it is
  the single highest-value test in the plan.
* `dbg_assert` invariants in every kernel under `BERNINI_GPU_DEBUG` stay permanently.
* Docs: `docs/culling.md`; `geometry_layout.md` gains the submesh bound;
  `passes.md` gains the new sub-passes and the two-phase frame shape.

**Gate:** the equivalence test in `just test`; docs landed; overlay and freeze mode demonstrated
in the editor.

---

## 5. Coexistence with the multi-context migration

The migration ([plan](multi_context_bgl_plan.md)) is mid-flight — S2b landed (contexts own queue,
list, allocator, framegraph, pass objects), S4–S7 have not. Rules of the road:

* **New pass state follows the per-context pattern.** The cull kernels and any scratch live on the
  pass objects `RenderContext` owns; like `CompactInstancesPass` today they are stateful and not
  re-entrant, which is fine because they are per-context. The migration plan's "per-context pass
  objects multiply scratch buffers" risk grows by whatever culling adds — keep pass-owned scratch
  minimal; the big allocations (HZB, visibility) are per-view, not per-pass, and would exist once
  per view under any architecture.
* **Nothing new lands on `Scene`.** The S5 shared-`Scene` decision is unmade. Culling reads the
  scene's geometry buffers and writes only per-view state on `SceneView` — it sits entirely on the
  read side of whatever S5 chooses, and adds zero new cross-context mutation. Keeping it that way
  is a review criterion for every checkpoint.
* **Single queue per context** (§2). No cross-queue fencing exists in the framegraph, and the
  queue topology is exactly what the migration is reshaping — do not add a second consumer of it.
* **Merge hazards are `RenderContext::Draw`'s attach order, `CompactInstancesPass`, and
  `ForwardPass`** — files both efforts touch. Land checkpoints as small PRs and rebase often.
  C4 is the widest diff; if S5's implementation is in flight when C4 is ready, sequence them
  rather than merging both into the same files in the same week.
* The stats/debug readback rides the existing per-context debug-readback ring, which the migration
  already made per-context — no new work, but it means stats are per-context, which is the correct
  scope anyway.

---

## 6. Designed-for futures (roadmap items this plan must not paint over)

* **Cascaded shadow maps** cull per light view. `CullView` (§2) is the provision: N cull
  invocations per frame with different matrices and no HZB, against the same kernels. What CSM
  adds later is per-view visibility outputs keyed by view, which the `SceneView`-owned layout
  already implies.
* **LODs.** LOD selection is a per-instance decision made at exactly the point `CullInstances`
  runs, with exactly its inputs (bounds, camera distance). The visibility *word* (§2) is the
  reserved space; selection writes an LOD index there and compact routes the instance to the
  right submesh range. No new pass will be needed.
* **Skinned meshes.** Posed geometry breaks the static instance bound — the bind-pose sphere is
  wrong once vertices deform. The provision is an overridable per-instance bounds source:
  conservative bind-pose-plus-slack bounds first, a post-skin bound later. Design the bounds fetch
  in the cull kernel behind one function now so the override is a local change. (This is also why
  cluster culling was dropped — see the scope note — since static meshlet bounds are just as wrong.)
* **TAA / motion vectors** need previous-frame view-proj plumbing; C4's history machinery
  establishes the "per-view previous-frame state" slot it will extend.
* **HZB particle collision** consumes the C3/C4 HZB. Export it under a stable framegraph name and
  keep the sphere-vs-HZB helper in shared shader code.
* **Foliage/grass** is the scale test: instance culling must stay one dispatch over N instances
  with zero CPU per-object work, which this design holds by construction.
* **WebGPU/Vulkan backends.** With cluster culling dropped, all culling is portable compute and
  carries to any backend directly. The mesh-shader geometry path (`ASBase` and the mesh shaders) is
  a separate backend concern — Vulkan gets it via `VK_EXT_mesh_shader`; WebGPU needs its own
  geometry path — but that is now orthogonal to culling.

---

## 7. Risks

* **Two-phase draw (C4) doubles pass count and interacts with barrier derivation.** The framegraph
  derives barriers from declared accesses, so correctness rides on declaring the HZB and history
  buffers precisely; `m_LastState` persistence across frames is exactly the machinery the
  migration plan flags as its likeliest corruption source. `--gpu-validation` is the gate, every
  checkpoint, not just the last.
* **Instance-slot instability vs. visibility history.** Epoch-clear (§C4a) is correct but may
  thrash in editing workflows where the scene mutates constantly — the stats overlay will show it
  as phase-2 spikes. Acceptable for the editor; the game's battle scenes mutate rarely.
* **The submesh sphere circumscribes the cooked AABB** — loose for elongated meshes (a sphere
  around a long box), so frustum culling under-culls (never over-culls). Watch the stats; a
  cooked tight sphere is an additive assetlib change if it matters. The cooked AABB itself is
  trusted as-is: a malformed one mis-culls rather than crashes, unlike the byte ranges the load
  path does validate.
* **D24S8 SRV portability.** The `R24_UNORM_X8` view is D3D12-specific plumbing in the one place
  it belongs (`bgl_d3d12`); the RHI surface only says "depth is sampleable". Vulkan has an
  equivalent; if a backend ever lacks it, the fallback is a depth copy, behind the same interface.
