# Per-view culling — implementation plan

Gives every culled frustum its own cull outputs, so a frame can hold more than one of them alive at
once. Today a view has exactly one set: cull it against a second frustum and the second overwrites the
first. `ROADMAP.md:76` asks for the opposite — *"Per-view culling — camera and each shadow cascade get
their own pass and indirect args"* — and cascaded shadow maps (`ROADMAP.md:256`) cannot be built until
that holds.

This is a *plan*, not a mirror of code. When the work lands, the durable parts belong in
[passes.md](../passes.md) — whose list of globally-imported buffers T2 and T3 falsify — and
[framegraph.md](../framegraph.md), which states the `ResolveName` rule T4 changes verbatim. This file
keeps the reasoning.

**The load-bearing property: no public API changes and no shader-visible layout changes.**
`IGraphics`/`IScene`/`ISceneView` are untouched, and the GPU structs keep their shapes. What changes is
*who owns* the cull output buffers and how many exist. A shadow cascade is not built here — this is the
seam it will need, delivered with one consumer (the camera) so it is exercised rather than speculative.

Background: [bgl design survey §7](bgl_design_review.md), which found the symptom. This plan revises
its prescription — see § 1.3.

---

## 1. What the survey found

### 1.1 One cull pipeline, its state scattered across three owners

Culling a view runs four compute passes, then a sort, then the forward draw reads what they produced.
The buffers that pipeline touches live in three places, split on no principle:

| Buffer | Lives on | Graph name | Imported as |
|---|---|---|---|
| instance, mesh, submesh, meshlet, vertex, index, materials | `SceneView` / `Scene` | `scene.instanceBuffer`, … | `ImportBuffer` (namespaced) |
| instance visibility | `SceneView` | `scene.instanceVisibility` | `ImportBuffer` |
| compacted instances | `SceneView` | `scene.compactedInstances` | `ImportBuffer` |
| sorted transparent, sort keys, sort count | `SceneView` | `scene.sortedTransparentInstances`, … | `ImportBuffer` |
| **PSO prefix sum** | `CompactInstancesPass` | `compactedInstances.psoPrefixSumBuffer` | **`ImportGlobalBuffer`** |
| **compact dispatch args** | `CompactInstancesPass` | `compactedInstances.compactDispatchArgs` | **`ImportGlobalBuffer`** |
| **cull view constants** | `CompactInstancesPass` | `cull.view` | **`ImportGlobalBuffer`** |
| **cull stats** | `CompactInstancesPass` | `cull.stats` | **`ImportGlobalBuffer`** |
| **transparent dispatch args** | `TransparentSortPass` | `transparentSort.dispatchArgs` | **`ImportGlobalBuffer`** |

`CompactInstancesPass::Init` allocates its four
([CompactInstancesPass.cpp:42-70](../../libs/bgl/src/passes/CompactInstancesPass.cpp)) and publishes
them at [:92-99](../../libs/bgl/src/passes/CompactInstancesPass.cpp); `TransparentSortPass` does the
same with one ([TransparentSortPass.cpp:65](../../libs/bgl/src/passes/TransparentSortPass.cpp)). Those
five are the **only** `ImportGlobalBuffer` call sites in the tree. `RenderContext` holds one instance
of each pass for the life of the device
([RenderContext.h:159-166](../../libs/bgl/src/gfx/RenderContext.h)), so those five buffers are
per-device.

### 1.2 Two *views* per frame already work; two *frustums* of one view do not

`RenderContext::Draw` appends `[compact, sort, forward]` per draw
([RenderContext.cpp:525-527](../../libs/bgl/src/gfx/RenderContext.cpp)), and the graph derives a UAV
barrier between draw *n*'s forward read and draw *n+1*'s compact write — `NeedsBarrier` returns true
for UAV-after-UAV even when the state is unchanged
([FrameGraph.cpp:67-75](../../libs/bgl/src/fg/FrameGraph.cpp)), and `m_Order` preserves submission
order. Two views render correctly, and
[RenderGeometry.cpp:237-284](../../libs/bgl/tests/src/RenderGeometry.cpp) covers exactly that.

**Today's behaviour is deterministic and correct — there is no race.** The defect is cardinality, not
synchronisation, and it is worth being precise because the two have different fixes.

Two *different* views each have their own `SceneView`, so their five view-owned buffers are distinct
allocations that both stay live; only the five device-owned ones are overwritten draw to draw. The
case that breaks is **one view culled against two frustums**, which is what a cascade set is: every
buffer in the pipeline, view-owned and device-owned alike, is singular per view, so frustum 1 destroys
frustum 0's visibility, compaction, prefix sum and dispatch args before anything reads them. The
shadow passes for cascades 0..N are recorded together and each reads its own compaction output.

### 1.3 The split on `SceneView` is wrong one level down too

The survey prescribed "move the pass buffers onto the view, as the visibility and transparent-sort
buffers already are." Reading the code, that is half right — **the buffers already on `SceneView` have
the same problem**. `m_InstanceVisibility`, `m_CompactedInstances`, `m_SortedTransparentInstances`,
`m_TransparentSortEntries` and `m_TransparentSortCount`
([SceneView.h:195-205](../../libs/bgl/src/scene/SceneView.h)) are all outputs of culling *one frustum*.
There is one of each per view, so a view culled against four cascades has one visibility word per
instance where it needs four.

There are two tiers conflated into one:

| Tier | What it is | Cardinality it needs |
|---|---|---|
| **Cull inputs** | instance, mesh, submesh, meshlet, vertex, index, material buffers | one per `SceneView` / `Scene` |
| **Cull outputs** | visibility, compacted, prefix sum, compact dispatch args, cull view | **one per culled frustum** |
| **Camera-only sort outputs** | sorted transparent, sort keys, sort count, transparent dispatch args | one per frustum that draws transparents — see D4 |

Moving the device-owned half onto `SceneView` would end the device-sharing and leave the cardinality
at one. That fixes the symptom, not the cause, and the cascade work would have to undo it.

---

## 2. Design decisions

### D1 — Cull outputs become their own type; a `SceneView` holds N

Introduce `CullState`: one frustum's cull outputs, with `Init`/`Resize`/`Release`. A `SceneView`
owns a vector of them, one per frustum it is culled against; the camera is index 0 and is the only one
this plan creates.

**Final membership is nine buffers**, arriving over three tasks: five from `SceneView` (T1 — compacted,
visibility, sorted transparent, sort keys, sort count), three from `CompactInstancesPass` (T2 — prefix
sum, compact dispatch args, cull view), one from `TransparentSortPass` (T3 — transparent dispatch args).
`m_CullStats` is not among them (D3).

It must expose the `ComputeBuffer` objects, not just their handles: the clear paths call `Clear(cmd)`,
which is a method on the object.

The passes stop owning storage. They keep their kernels — a PSO is immutable and genuinely per-device.

**Rejected: move the pass buffers onto `SceneView` beside the existing scratch.** The survey's
proposal. Smaller diff, and it ends the device-sharing. Rejected because it leaves tier 2 singular, so
cascades would need one `SceneView` per cascade — duplicating the instance and mesh buffers, the
largest per-view allocations at crowd scale and exactly what the Scene/View split exists to avoid.

**Rejected: keep the buffers on the pass, pooled by draw index.** Smallest change, and each draw does
get its own set. Rejected because the pass would own storage whose lifetime and size belong to a view:
the pool must be sized by max-draws-per-frame with nothing to resize it when a view's instance count
grows, and `Pass::Release` would free memory the view logically owns.

**Rejected: one arena indexed by `cullViewIndex * capacity + slot`.** Fewer allocations, one descriptor
per buffer instead of N, indexing free in the shader. Genuinely attractive. Rejected *for now* because
growth becomes all-or-nothing across every frustum — the existing path resizes one buffer at a time
([SceneView.cpp:149-164](../../libs/bgl/src/scene/SceneView.cpp)) — and because it changes shader-side
indexing, which the load-bearing property says this plan will not. Revisit once cascades exist and N is
known.

### D2 — Two namespace scopes per draw: inputs stay at `v{n}:`, outputs move to `v{n}c{k}:`

This is the correction that matters most, and the obvious version of it is wrong.

The tempting design is to extend the one namespace from `v{n}:` to `v{n}c{k}:` and let every pass keep
naming `scene.instanceVisibility`. **That breaks the tier-1 buffers.** They are not global imports —
`Scene::ImportResources` ([Scene.cpp:449](../../libs/bgl/src/scene/Scene.cpp)) and
`SceneView::ImportResources` ([SceneView.cpp:512](../../libs/bgl/src/scene/SceneView.cpp)) both call
`ImportBuffer`, so they already carry whatever namespace is current. Under a single `v{n}c{k}:` scope
there are only two outcomes, and both are defects:

- **Import once, under `c0`.** A pass recorded under `v0c1:` resolves neither the scoped key nor a bare
  one, so `ResolveName` returns the scoped miss
  ([FrameGraph.cpp:171-183](../../libs/bgl/src/fg/FrameGraph.cpp)), `Compile` treats it as a transient,
  and `PassContext::GetBuffer` throws at execute.
- **Import per `k`.** One `BufferHandle` now lives under N keys, each with its own tracked `current`
  state, so key *k*'s barrier is derived from key *k*'s stale history rather than the buffer's. That is
  precisely the aliasing `ImportGlobalBuffer` exists to prevent.

So the scopes have to be split: **tier-1 and Scene-owned buffers stay imported under `v{n}:`; only the
`CullState` buffers are imported under the cull scope.** A cull pass sets the cull scope for its own
declarations and resolves tier-1 names by falling outward to the view scope. That fall-back is
currently bare-name-only, so **`ResolveName` must learn to walk outward through prefixes** — the one
piece of `FrameGraph` this plan changes, landing in T4.

**The cull scope must be spelled `v{n}:c{k}:`, not `v{n}c{k}:`.** A view's prefix already ends in a
colon ([SceneView.cpp:49](../../libs/bgl/src/scene/SceneView.cpp) formats `"v{}:"`), so tier-1 is keyed
`v0:scene.instanceBuffer`. From a scope spelled `v0c1:` there is no prefix — by `:`-segment or by
character — that yields `v0:`; dropping the single trailing segment gives `""`, and the walk lands on
the bare name, misses, and reproduces failure mode 1 above. Spelled `v0:c1:`, dropping the last
`:`-delimited segment gives `v0:`, and the walk is three tries: `v0:c1:` → `v0:` → bare.

There is no existing coverage of `SetResourceNamespace` or `ResolveName` in `FrameGraph_test`, so T4's
case is the first — which raises what that gate is worth.

**Rejected: encode the index in the name** (`scene.instanceVisibility.2`). Puts format strings on the
per-frame path in every pass — the survey's §5 already objects to how many there are — and threads the
index into every `AddBufferArg`.

**Pass names need the index too.** Pass names are unique *globally*, not per namespace
([FrameGraph.cpp:633-638](../../libs/bgl/src/fg/FrameGraph.cpp)), and every one is keyed on
`draw.drawIdx` alone (`std::format("Cull Instances {}", draw.drawIdx)` and its siblings). N frustums
recorded under one `Draw` collide by name and `AddPass` throws. T4 must key pass names on
`(drawIdx, cullIdx)`.

### D3 — Cull stats stay device-wide and are not touched

`m_CullStats` is written by the cull shader under `BERNINI_GPU_DEBUG`
([CullInstances.slang:82-88](../../libs/bgl/shaders/src/CullInstances.slang)) and bound whenever
reflection kept the handle ([CompactInstancesPass.cpp:229-234](../../libs/bgl/src/passes/CompactInstancesPass.cpp)).
Nothing on the CPU reads it — the "read back for the stats overlay" comment at
[CompactInstancesPass.h:71](../../libs/bgl/src/passes/CompactInstancesPass.h) describes an overlay that
does not exist, and `CullInstances_test` allocates its own buffer rather than using the pass's.

Deleting it here would be wrong: `BERNINI_GPU_DEBUG` is on for every Debug build
([CMakeLists.txt:17](../../CMakeLists.txt)), which is the only config that builds the test suites, so
removing the buffer leaves a live shader write bound to nothing — surfacing under T2's own validation
gate as a confusing mid-task failure.

It therefore **stays exactly where it is**, device-wide on the pass, keeping its `ImportGlobalBuffer`.
Deleting it is on the survey's small-items list and belongs in its own PR to `master`, where the
shader write and the uniform field can go with it.

**Residue this leaves after T4.** `cull.stats` stays one resolved key that every frustum's Clear pass
writes as copy-dest and every frustum's Cull pass writes as UAV, so the graph's last-writer edge
chains all N frustums' cull passes into one dependency chain and emits a barrier between them. Harmless
while `m_Order` is submission order regardless, but it is the one cross-frustum edge surviving this
plan — worth knowing in a subsystem whose next roadmap item is async compute
([ROADMAP.md:252](../../ROADMAP.md)). Deleting the buffer removes the edge with it.

**Rejected: migrate it to `CullState`.** Multiplies a buffer nobody reads by the cascade count —
and it could not aggregate across frustums even if something did read it, since `ExecuteClear` zeroes
it per draw ([CompactInstancesPass.cpp:198](../../libs/bgl/src/passes/CompactInstancesPass.cpp)) and
under T4 that clear is per frustum.

### D4 — The transparent sort buffers move, and the waste is accepted for now

`m_SortedTransparentInstances`, `m_TransparentSortEntries`, `m_TransparentSortCount` and the pass's
`m_DispatchArgs` depth-sort transparents against a camera position
([TransparentSortPass.cpp:152](../../libs/bgl/src/passes/TransparentSortPass.cpp)). **A shadow cascade
does not sort transparents**, so every cascade allocates two `paddedInstances`-sized buffers — 4 bytes
per instance for the sorted list, 8 for the `uvec2` keys — plus two single-element ones, none of which
anything reads.

They move into `CullState` anyway, and the waste is accepted, because the alternative — splitting
tier 2 into "cull outputs" and "camera-only sort outputs" — adds a second type and a second lifetime
before there is a consumer that needs the distinction. When cascades land and N is real, the split and
the D1 arena are the same follow-up and should be taken together.

Recorded here so it is a decision rather than an oversight.

---

## 3. What changes

| File | Change | What could break |
|---|---|---|
| `scene/CullState.h/.cpp` *(new)* | the tier-2 set: init, resize, release, import | — |
| `scene/SceneView.h/.cpp` | holds `std::vector<CullState>`; five buffers move out | **`GetInstanceBuffers()` welds the tiers**: `m_CompactedInstances` rides the tuple beside two tier-1 inputs ([SceneView.h:111](../../libs/bgl/src/scene/SceneView.h), folded at [SceneView.cpp:504-516](../../libs/bgl/src/scene/SceneView.cpp)). T1 has to split it — **and shorten `c_InstanceBufferInfo` ([:18-22](../../libs/bgl/src/scene/SceneView.cpp)) in the same commit**, since the fold pairs the two positionally with `i++`. Shorten one alone and a name binds to the wrong buffer with no diagnostic. |
| | | `SyncInstanceScratch` resizes the four `paddedInstances`-sized buffers and early-outs on `m_CompactedInstances` alone ([:157-158](../../libs/bgl/src/scene/SceneView.cpp)) — every frustum must grow together or a cull writes out of bounds. `m_TransparentSortCount` is a single `uint32_t` made in `InitBuffers` ([:86-93](../../libs/bgl/src/scene/SceneView.cpp)) and correctly never resized, so `CullState` has two creation lifetimes to absorb, not one. |
| | | **`SceneView::Update` retires three of them by hand** ([:460-464](../../libs/bgl/src/scene/SceneView.cpp)) precisely because they are not in `GetInstanceBuffers()`, and `ImportResources`' `resourceNames` out-param feeds the copy-dest args of the `SceneView Update {drawIdx}` pass ([:483-497](../../libs/bgl/src/scene/SceneView.cpp)). Both must move with the buffers, or that pass — recorded under `v{n}:` — declares args naming nothing. |
| `passes/CompactInstancesPass.*` | drops three buffers (keeps `m_CullStats`, D3); takes the set from `DrawData` | the intra-pass UAV barrier at [:281-287](../../libs/bgl/src/passes/CompactInstancesPass.cpp) must follow the buffer, not the pass |
| | | **the clear and dispatch halves reach the same buffer two different ways**: `ExecuteClear` writes through the pass's own members ([:197-211](../../libs/bgl/src/passes/CompactInstancesPass.cpp)) while `ExecuteCull` binds `ctx.GetBuffer("cull.view")` ([:223](../../libs/bgl/src/passes/CompactInstancesPass.cpp)). Move the import without moving the clear and the graph barriers the view-owned buffer while the seed lands in the orphaned device-wide one — no throw, no validation error. |
| `passes/TransparentSortPass.*` | drops `m_DispatchArgs`; same import change | the sort's capacity cap becomes per-frustum; `ExecuteClear` has the same split in one function ([:130 vs :136](../../libs/bgl/src/passes/TransparentSortPass.cpp)) |
| `passes/ForwardPass.cpp` | reads `compactedInstances.*` / `transparentSort.*` from the namespace | barrier states must still agree across the rename |
| `gfx/RenderContext.cpp` | two namespace scopes; `DrawData` carries the cull index | the ordering accident of §1.2 stops being load-bearing — the point of the change |
| `fg/FrameGraph.cpp` | `ResolveName` walks outward through prefixes; nothing else | **the sharpest hazard** — see below |

`ResolveName` prefers `ns+name`, then falls back to the bare `name`
([FrameGraph.cpp:171-183](../../libs/bgl/src/fg/FrameGraph.cpp)). So during migration a stale *bare*
import of a name silently wins, while a *missing* import throws at `GetBuffer`. Failure is silent in
one direction and loud in the other. **Each of T2 and T3 must move a buffer's import and every one of
its consumers in the same commit.**

**T4 removes the loud direction.** Once the walk falls outward, a tier-2 buffer left imported at
`v{n}:` instead of moving to the cull scope resolves silently through the walk and aliases one
view-level buffer across all N frustums — no throw, and exactly the aliasing D2 rejects. After T4 the
only thing separating correct from silently-wrong is which scope was current at import, so T4's
`FrameGraph_test` case should pin the walk's precedence, not just that it resolves.

**The walk is import-only, by design.** `ResolveName` consults `m_Imported` and nothing else, while
`Compile` keys the producer map on the resolved name — which for a transient is the full scoped miss.
A transient produced under one scope and named from another would become two independent resources: no
dependency edge, no barrier, and a pass whose only output is that transient culled as dead. Nothing
crosses that boundary today (every arg the migrating passes declare is imported), so this is something
T4's test should pin rather than a defect to fix.

---

## 4. Tasks

Each is one PR into `feat/per-view-culling`, in order, each green on its own.

**On GPU validation.** Every task below moves UAV barriers, so each wants a validation run — but
`--gpu-validation` is read only by `Graphics_d3d12` and **does nothing on Metal**
([libs/bgl/CLAUDE.md:47](../../libs/bgl/CLAUDE.md)). Wherever this plan says "validation run", it means
whichever of these matches the machine:

```bash
just run bgl_tests -- --gpu-validation                                   # D3D12
METAL_DEVICE_WRAPPER_TYPE=1 MTL_SHADER_VALIDATION=1 just run bgl_tests   # Metal
```

A task verified with the wrong one has not been verified at all; say in the PR body which ran.

**On the unit suites.** `CullInstances_test`, `CompactInstances_test`, `HistogramInstances_test`,
`TransparentSort_test` and `TransparentDepthKeys_test` do **not** instantiate the passes — each builds
its own frame graph mirroring the pass's declarations, as `CompactInstances_test.cpp:153` says outright
("Pass declarations mirror CompactInstancesPass. Diverge from them and this test stops standing"). They
gate the *shaders*, which no task here touches, and would pass with the pass wiring destroyed. They
must stay green because a task that changes a pass's declarations without updating its mirror silently
puts the two out of step — but the gate that catches a broken migration is the full-suite run, the
end-to-end tests that call `DrawFrame`, and the goldens.

### T1 — `CullState`, holding what `SceneView` already owns

Extract the five view-owned tier-2 buffers into the new type; split `GetInstanceBuffers()`. `SceneView`
holds exactly one, so nothing observable changes.

**Gate:** `just test bgl` in full, including `RenderGeometry`'s two-scenes-in-one-frame section — which
renders two scenes and compares the result against `sphere_cube.exp.png`, the *one*-scene expectation,
so it pins that a second view changes nothing. Every golden unchanged; a diff means the extraction
moved behaviour, not just code.

### T2 — `CompactInstancesPass` stops owning storage

Prefix sum, compact dispatch args and cull view move into `CullState`; `ImportGlobalBuffer` →
`ImportBuffer` for those three. `m_CullStats` stays (D3).

**Gate:** the full-suite validation run above — this task moves UAV barriers, which validation
catches and the ordinary suite does not. `sphere_cube.exp.png` and `pbr_ibl.exp.png` are the
behavioural net (the two-scenes section compares against `sphere_cube.exp.png`); the mirror suites stay
green.

### T3 — `TransparentSortPass` stops owning storage

`m_DispatchArgs` moves in; same import change; `ForwardPass` follows the name.

**Gate:** `Transparent_test`, which is the only transparent test that goes through the pass (it calls
`DrawFrame`) — note it is **not** a golden comparison: it renders two orderings and checks they match,
plus centre-luma and `r > b` assertions so a sort producing nothing cannot pass silently. There are no
`transparent_*` goldens. Plus the full-suite validation run: T3 moves the same class of barrier T2
does.

### T4 — N per view, indexed by the namespace

`SceneView` holds a vector; `RenderContext` sets two scopes; `ResolveName` walks outward; pass names key
on `(drawIdx, cullIdx)`; `DrawData` carries the index. The camera is index 0 and stays the only
consumer.

**Gate:** a new `PerViewCulling_test` that culls **one view against two different frustums in one
frame** and reads back both compaction outputs, asserting each matches its own frustum — the thing that
is impossible today, so the gate that proves the feature. A CPU-side reference cull over the same
instances supplies the expected sets, in the spirit of `ROADMAP.md:77`'s culling-verification item.
Plus a `FrameGraph_test` case pinning the outward prefix walk, since that is the one graph change.

### Not in scope

Shadow cascades; a light abstraction; the D1 arena and the D4 split (one follow-up, taken together);
deleting cull stats (its own PR to `master`); the `m_LastState` growth at survey §4 — though note T4
multiplies it, since per-frustum keys are retained per frustum and view ids never recycle.

---

## 5. Verification for the feature as a whole

`just build && just test`, then the validation run for the machine's backend (see § 4), since every
task moves barriers or UAV state. The goldens are the regression net for what has no assertion: a
change in `sphere_cube.exp.png` or `pbr_ibl.exp.png` means a task moved behaviour it was only meant to
relocate.
