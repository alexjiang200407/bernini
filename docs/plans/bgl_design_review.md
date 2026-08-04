# bgl — design and architecture survey

A read of `libs/bgl` against what it is for: the public surface (`IGraphics`/`IScene`/`ISceneView`),
the RHI beneath it, the frame graph, the scene/view split, the pass layer, and both backends' `Graphics`
classes. Ten findings, ranked by what they cost the *next* feature rather than by how wrong they are today.

**This is a survey, not a plan.** Nothing here is scheduled and nothing here is a defect report — the
engine renders correctly. What it records is where the current shape will fight the work that comes
next, and what the cheap moment to change it looks like. Items that graduate into work want plans of
their own; the durable parts of any fix belong in [bgl_api.md](../bgl_api.md), [rhi.md](../rhi.md) and
[framegraph.md](../framegraph.md), not here.

Line references are against master at the time of the read and will drift. In particular this read
predates `feat/taa`, which reworks the frame path substantially — HDR scene colour, a tonemap pass, a
TAA resolve pass, and additions to `PsoType` — so §3, §5, §7 and §8 describe a `RenderContext` and a
PSO table that branch has already moved. The findings hold; the line numbers in those sections will
not survive it.

---

## 1. Immutable instance transforms are deliberate — but the velocity contract is unwritten

`ISceneView` exposes `CreateStaticMeshInstance(geom, transform)` and `DeleteMeshInstance`, and nothing
else that touches placement ([ISceneView.h:37-49](../../libs/bgl/include/bgl/ISceneView.h)). The
motion-vector path therefore reprojects through `viewProj` / `prevViewProj` only
([DrawData.h:18-19](../../libs/bgl/src/passes/DrawData.h),
[ForwardPass.cpp:350-351](../../libs/bgl/src/passes/ForwardPass.cpp)) — velocity is camera motion
only, by construction.

**This is a recorded decision, not an oversight**, and the obvious fix fights it.
[ROADMAP.md:80](../../ROADMAP.md) says so outright: *"Instance transforms are immutable, so this is
camera motion only; the mesh shader hands the pixel stage both clip positions, which is the seam the
skinned and VAT paths extend."* Units move by GPU crowd simulation writing instance data GPU-side, not
by a CPU setter — the guiding constraints are explicit that *"per-unit CPU updates are the enemy"*
(ROADMAP.md:28). A `SetInstanceTransform` on `ISceneView` would be the wrong shape at every scale the
engine is designed for, and it would put a CPU write-path in front of the one thing the roadmap wants
GPU-resident.

What *is* worth recording is the unwritten part: nothing in the public docs tells a velocity-buffer
consumer that its contents are camera-only. TAA is roadmapped
([ROADMAP.md:85](../../ROADMAP.md)) and correct against that assumption today; the assumption is
load-bearing and undocumented. That is a paragraph in [bgl_api.md](../bgl_api.md), not a feature.

## 2. Lights are absent, and what replaces them is bounded but not trivial

The entire lighting model is IBL plus skybox: `SetEnvironmentMap`, `SetSkyBox`, `SetExposure`.
[libs/bgl/CLAUDE.md](../../libs/bgl/CLAUDE.md) says bgl "should provide higher level abstractions of
Mesh, Light, Material"; Light does not exist in any form.

The roadmap bounds the shape without reducing it to one light. The guiding constraint at
[ROADMAP.md:24-26](../../ROADMAP.md) reads *"**One dominant light.** Forward rendering with a single
sun keeps shading cheap. Do not invest in clustered/tiled many-light infrastructure"* — and Light and
Shadow ([ROADMAP.md:251-258](../../ROADMAP.md)) lists point light and ambient/sky light beside the
directional one. The two resolve: the sun stays *dominant* and shading is built around it, while the
others are secondary contributors few enough to loop over. So bgl does need somewhere for a small
light set to live; what it must not grow is the culling and bucketing machinery that makes thousands
of them cheap.

The structural finding underneath is separable from that, and it is §7 below: each shadow cascade
needs its own cull pass and its own indirect args ([ROADMAP.md:76](../../ROADMAP.md)), and the
per-context scratch in `CompactInstancesPass` is what stops a frame having more than one culled view
**live at the same time** — which is precisely what a cascade set is, since each cascade's compaction
output has to survive while the next one is culled. **Cascades are blocked on per-view cull state,
not on a light abstraction** — the two can be built in either order.

## 3. `PsoType` is a hand-enumerated cross product with no enforcement

[idl/src/PsoType.slang](../../libs/bgl/idl/src/PsoType.slang) enumerates
GeomType × MaterialType × LayerType × occlude by hand. Adding one material type means coordinated
edits at every one of these sites:

| Site | What breaks if it is missed |
|---|---|
| `idl/src/PsoType.slang` | four new entries |
| `util.cpp:98` `GetPsoFromGeomAndMaterial` | nested switch, `gfatal` fallthrough |
| `ForwardPass.cpp:153` `c_Psos` | **order must match the enum**; guarded only by a "no empty `pixelSrc`" `static_assert` |
| `util.cpp:139,148` `IsTransparentPso` / `IsOccludeTransparentPso` | hand-listed enum values |
| `Scene.h:244`, `Scene.cpp:1021` | new `EntryBuffer` member, new `DeleteMaterial` case |
| `IScene.h` | new `Create*` / `Update*` virtual pair |

The order-matching invariant between an IDL-generated enum and a C++ array is the sharp edge. A
reordered enum silently gives every material the wrong pipeline, and the `static_assert` that exists
does not catch it — it only checks that no row is value-initialized.

The enum also claims distinctions the renderer does not make. All four transparent rows use
`c_TransparentSrc`, and `DrawTransparent` hardcodes the `_PBR` kernel slot to draw LoosePbr instances
too ([ForwardPass.cpp:494-506](../../libs/bgl/src/passes/ForwardPass.cpp)) — four enum values, two
real pipelines.

The shape that scales is deriving the bucket from its axes: material type selects the pixel shader,
layer and occlude select render state. Enumerating the product does not.

## 4. `FrameGraph::m_LastState` grows for the process lifetime

`ClearFrame` deliberately preserves `m_LastState` so cross-frame resource states survive
([FrameGraph.cpp:617-624](../../libs/bgl/src/fg/FrameGraph.cpp)), which
[framegraph.md](../framegraph.md) documents as load-bearing. But its keys are namespace-prefixed
names, and the prefix carries an id from a monotonic atomic that never recycles
([SceneView.cpp:49](../../libs/bgl/src/scene/SceneView.cpp)).

**The view is the only namespace that exists.** `RenderContext::Draw` sets the namespace to the
*view's* prefix and then attaches both the scene and the view under it
([RenderContext.cpp:415-418](../../libs/bgl/src/gfx/RenderContext.cpp)), so a scene buffer is keyed
`v7:scene.submeshBuffer`, not `s3:…`. `Scene::ResourceNamespace()`
([Scene.h:105](../../libs/bgl/src/scene/Scene.h)) exists and has no caller anywhere in `libs/`,
`apps/` or `examples/` — the `s{n}:` prefix is never installed. That is worth deleting on its own; a
namespace accessor that is never applied is a trap for whoever next reasons about key shapes.

So a scene leaves no keys of its own, and **every view ever created leaves 14** — seven from
`Scene::ImportResources` and seven from `SceneView::ImportResources` — permanently, after it is
destroyed. The `ImportGlobalBuffer` keys (`cull.view`, `cull.stats`, `compactedInstances.*`) skip the
prefix and are bounded. The editor's thumbnail cache and material-preview windows each stand up views;
a session that opens and closes them accumulates dead state indefinitely. Nothing retires a name,
because names are not owned by anything.

Two ways out: evict on "not imported for N frames", or key tracked state on the resource handle rather
than on the string. Note that the leak is per *view*, so an eviction hook hung off scene teardown
would catch nothing.

## 5. The frame path is built out of heap strings

Related to §4 but separable. Every pass, every draw, every frame allocates:
`std::format("Forward {}", drawIdx)`, a `std::string` at each of the four sites that *name* the
compact-dispatch-args buffer — an `ImportGlobalBuffer` and two `AddBufferArg`s in
`CompactInstancesPass.cpp` (96, 108, 184), which repeat the raw literal, plus the `BufferArg` in
`ForwardPass.cpp:299`, the only one going through the `c_DispatchArgsBuffer` constant — one
`std::string` per imported buffer in `Scene::ImportResources` and `SceneView::ImportResources`, plus a
`std::vector<std::string>` to carry them ([Scene.cpp:418](../../libs/bgl/src/scene/Scene.cpp),
[SceneView.cpp:484](../../libs/bgl/src/scene/SceneView.cpp)). Resolution is then hash-map lookups on
those strings, and `FindPass` is a linear scan
([FrameGraph.cpp:632](../../libs/bgl/src/fg/FrameGraph.cpp)).

The declarative frame graph is the right call. String *identity* as its runtime representation is the
part to revisit — interned ids assigned at import, with the strings kept for diagnostics only, keeps
the entire design and drops the per-frame allocation.

## 6. `Graphics` is written twice

[Graphics_d3d12.cpp:19-138](../../libs/bgl/src/d3d12/Graphics_d3d12.cpp) and
[Graphics_metal.cpp:97-243](../../libs/bgl/src/metal/Graphics_metal.cpp) each define a complete
`Graphics` class. Roughly fifteen methods in each are byte-identical one-line forwarders to
`RenderContext`. The genuinely backend-specific parts are device creation, the D3D12 debug/info-queue
plumbing, and Metal's `FrameCapture`.

`GraphicsBase.h` already exists as the shared interface — it just carries no shared *implementation*.
Every new `IGraphics` method has to be added in three places today, and the two copies have already
drifted: Metal wraps `BeginFrame`/`EndFrame` for capture, D3D12 wraps nothing.

The fix is to pull the forwarders into `GraphicsBase` and leave the backends a `CreateDevice`-shaped
hook plus optional frame hooks.

## 7. Per-context scratch shared across draws, with an undocumented ordering invariant

`CompactInstancesPass` owns `m_CompactedDispatchArgs`, `m_PsoPrefixSumBuffer`, `m_CullView` and
`m_CullStats` as a single instance for the whole context
([CompactInstancesPass.h:60-72](../../libs/bgl/src/passes/CompactInstancesPass.h)), published with
`ImportGlobalBuffer`. Two `RenderJob`s in one frame both write them.

This is correct today only because `RenderContext::Draw` adds passes as
`[compact_n, sort_n, forward_n]` per draw, so the graph's UAV barrier serializes draw *n*'s forward
read against draw *n+1*'s compact write. Reordering
[RenderContext.cpp:465-467](../../libs/bgl/src/gfx/RenderContext.cpp) — or ever recording two views
concurrently — breaks it into flicker of exactly the kind [framegraph.md](../framegraph.md) already
records two bug precedents for.

Nothing *states* that invariant. The net that exists is
[RenderGeometry.cpp:237-284](../../libs/bgl/tests/src/RenderGeometry.cpp), `SECTION("Two scenes in one
frame (cube + sphere)")`, which draws two views between one `BeginFrame`/`EndFrame` and compares
against a golden image — the right scenario, but a golden image is a weak pin for a nondeterministic
UAV hazard, and it passes for the wrong reason if the race happens to resolve favourably.

The same applies to `ForwardPass`'s retained `m_Kernels`, whose `Uniforms` CPU mirrors are mutated
between dispatch recordings.

Per-draw scratch belongs on the view, as the visibility and transparent-sort buffers already are. If
it stays shared, the ordering has to become an explicit asserted contract rather than an accident of
`Draw`'s statement order.

**This is the item on the critical path.** [ROADMAP.md:76](../../ROADMAP.md) requires *"Per-view
culling — camera and each shadow cascade get their own pass and indirect args"*, and cascaded shadow
maps ([ROADMAP.md:256](../../ROADMAP.md)) cannot land until that holds. Two culled views in one frame
already work — the graph serialises them — but only one at a time: draw *n*'s compaction output is
overwritten by draw *n+1* before anything could read both. A cascade set needs every cascade's
compaction output live simultaneously, and one `CullView`, one prefix-sum buffer and one
dispatch-args buffer per *context* is exactly what cannot supply that. Every other finding here is a
tax; this one is a wall.

## 8. Uniform binding is name-keyed and reflection-driven on the hot path

`BindKernel` re-binds roughly twenty uniforms per PSO bucket, per draw, per frame
([ForwardPass.cpp:332-399, 425-446](../../libs/bgl/src/passes/ForwardPass.cpp)). The bucket loop skips
the transparent PSOs, so it issues six `DispatchMeshIndirect` calls, and `DrawTransparent` adds three
fixed ones — nine per draw, whatever the scene holds, each bucket bound and dispatched whether or not
a single instance landed in it. (`Execute` does early-out when the *view* has no instances at all.)
Each
`kernel["cbuffer"]["member"] = value` is a hash lookup plus a virtual `UniformsNode::Traverse`
([Uniforms.h:66-88](../../libs/bgl/src/uniforms/Uniforms.h)). There is no cached `(offset, type)` path
from a binding that was already resolved once.

Reflection-driven binding is a good *authoring* model. It should not also be the runtime
representation: resolve to offsets at `Init` and bind against those.

Two adjacent things in the same code:

* `BindSceneBuffers` calls `gfatal` on a missing uniform key
  ([ForwardPass.cpp:114](../../libs/bgl/src/passes/ForwardPass.cpp)) — a hard process abort in
  per-frame render code, for a shader/CPU mismatch that is fully knowable at `Init`.
* The per-bucket dispatch is unconditional on the bucket being non-empty, so adding material types
  linearly adds no-op dispatches to every draw, compounding §3.

## 9. Multi-queue stops one step short, and the last step is the hard one

`QueueType` has `kCompute` and `kCopy`, `PassDesc::SetQueue` exists
([PassDesc.h:114](../../libs/bgl/src/fg/PassDesc.h)), `FrameGraph::RegisterQueue` takes a name, and the
RHI has `InsertWaitForQueue`. `RenderContext` registers exactly one queue, `"main"`
([RenderContext.cpp:357](../../libs/bgl/src/gfx/RenderContext.cpp)).

**Misuse is not silent** — `Execute` throws on a pass whose queue was never registered
([FrameGraph.cpp:489-496](../../libs/bgl/src/fg/FrameGraph.cpp)), a contract
[framegraph.md](../framegraph.md) already states. So `SetQueue("compute")` aborts the frame rather
than desynchronising it, and there is nothing unsafe to remove.

What is missing is the piece that makes the feature usable: a caller who *does* `RegisterQueue` a
second queue gets passes recorded onto its list with **no derived cross-queue fences**, because
`Execute` neither submits nor waits — [framegraph.md](../framegraph.md) puts that on the caller. Every
other barrier in the frame is derived; this one is hand-rolled, which is the inversion worth fixing.

Async Compute is roadmapped, under Light and Shadow ([ROADMAP.md:252](../../ROADMAP.md)), so this is a
gap that will be walked into rather than dead surface. Deriving the cross-queue waits in `Compile` —
where the last-writer edges the graph already computes are exactly the fence points — is the shape.

## 10. The DLL seam that justifies the interface design is never tested

[bgl_api.md](../bgl_api.md) is explicit: the API is pure-virtual and intrusively refcounted *because
bgl is a DLL* — "an ABI seam, not backend polymorphism." But `bgl_tests` links `bgl_objects` and
`$<TARGET_OBJECTS:bgl_d3d12>` directly ([libs/bgl/CMakeLists.txt](../../libs/bgl/CMakeLists.txt)), so
every test runs fully statically linked. The one property the abstraction exists to provide is
exercised only by the editor and the examples.

Separately in the same file: `bgl` both embeds `$<TARGET_OBJECTS:bgl_objects>` and calls
`target_link_libraries(bgl PUBLIC bgl_objects ...)`. Linking an `OBJECT` library also contributes its
objects — worth confirming those are not included twice.

---

## Smaller items

* **`RenderContext::Draw` downcasts without checking.** `Ref::As<T>()` is `dynamic_cast`
  ([core/ref/Ref.h:23](../../libs/core/include/core/ref/Ref.h)). `BeginFrame` gasserts its target
  ([RenderContext.cpp:315](../../libs/bgl/src/gfx/RenderContext.cpp)) but `Draw` does
  `job.view->As<SceneView>()` then `view->GetScene()->As<Scene>()` and dereferences both unchecked
  (`:399-400`). `ISceneView` is an exported pure-virtual interface, so a foreign implementation is
  representable in the type system and lands as a null deref. It is also two `dynamic_cast`s per draw
  per frame.

* **Cull stats are dead.** `m_CullStats` is allocated, cleared every frame and written by the cull
  shader; nothing on the CPU reads it outside `CullInstances_test.cpp`. The comment at
  [CompactInstancesPass.h:71](../../libs/bgl/src/passes/CompactInstancesPass.h) claims it is "read back
  for the stats overlay" — there is no overlay.

* **Two docs give opposite lifetime contracts.**
  [ISceneView.h:41-43](../../libs/bgl/include/bgl/ISceneView.h) says `DeleteMeshInstance` decrements
  "the shared Scene's reference count for that geometry." No such refcount exists, and
  [IScene.h:265-269](../../libs/bgl/include/bgl/IScene.h) documents the opposite — the scene does not
  track instances and cannot check. One of these will be believed.

* **`Scene::GetBuffers()` returns a `std::tie` of seven internal buffers**, and `ImportResources`
  walks it by index against a parallel `c_BufferInfo` name array with a manual counter
  ([Scene.cpp:440-453](../../libs/bgl/src/scene/Scene.cpp)). Reordering the tie silently renames every
  buffer in the frame graph. The comment above `c_BufferInfo` already says the order "MUST stay in
  lockstep" with two other lists; a `static_assert` on the arity is the minimum that enforces it.

* **PNG encoding lives in `RenderContext.cpp`** (`STB_IMAGE_WRITE_IMPLEMENTATION`, line 13). The
  layering rule is that bgl stays codec-free; `ScreenshotPng` is a standing exception.
  `SubmitCapture` / `ScreenshotToMemory` already return `ImageData`, so the file-writing convenience
  could live in the caller.

* **Procedural primitives are in the renderer's public API.** `AddCubeGeom` / `AddSphereGeom` /
  `AddPlaneGeom` ([IScene.h:131-159](../../libs/bgl/include/bgl/IScene.h)) are authoring
  conveniences; they read like `gamelib` or a test utility rather than part of bgl's surface.

---

## Where to start

**§7 is the only wall.** Cascaded shadow maps need per-view culling
([ROADMAP.md:76,256](../../ROADMAP.md)), and one set of cull/compaction scratch per *context* is what
stands in the way. It is also the only finding here big enough to want a feature branch: it touches
`SceneView`, `CompactInstancesPass`, `TransparentSortPass` and `RenderContext::Draw` together, and
master would be broken in the middle of it.

Everything else is a tax rather than a blocker, and most of it is a single PR to master:

| | Shape |
|---|---|
| §3 `PsoType` derivation | its own feature; taxes every material type added from here on |
| §4 `m_LastState` growth | one PR — the only item that degrades a *running* process |
| §5 string identity, §8 uniform binding | perf work, wants a measurement before a rewrite |
| §6 duplicated `Graphics` | one PR, mechanical |
| §9 cross-queue fence derivation | a feature, and Async Compute is what will need it |
| §10, cull stats, dead `Scene::ResourceNamespace`, doc contradictions, `gfatal`→`Init` | one PR each, small |

§1 needs a documented velocity contract and no code. §2 needs somewhere for a small light set to
live, on whatever schedule Light and Shadow takes.
