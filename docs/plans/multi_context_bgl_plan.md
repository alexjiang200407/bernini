# Multi-context bgl — implementation plan

Implements [the multi-context spec](../specs/multi_context_bgl.md): give bgl N independent submission
contexts over one device, so a client's blocking work stalls only that client.

This is a *plan*, not a mirror of code. It records the staging, the verification gate at each
stage, the one decision deliberately left open, and the hazards found while surveying the code.
When the work lands, the durable parts move to [rhi.md](../rhi.md) and a new `docs/render_contexts.md`.

**Naming.** The unit is **`IRenderContext`**, created by `IGraphics::CreateRenderContext`. The name
is currently taken by the per-draw `{view, camera, viewport}` POD, which is renamed **`RenderJob`**
in S0c to free it.

The reuse is deliberate, not opportunistic. This is the D3D11 device/context split: `ID3D11Device`
is free-threaded and creates resources, while an `ID3D11DeviceContext` is thread-affine and records
commands, with one immediate and N deferred contexts over a single device. `IGraphics`/`IDevice`
and `IRenderContext` divide along exactly that line. "Context" has meant *the thread-affine handle
you issue rendering commands through* since GL contexts, so the threading contract is legible from
the name alone.

Names built on the POD — `RenderJobQueue`, `RenderJobPool` — were considered and rejected: both are
containment metaphors, and a context holds no jobs. `Draw(job)` records immediately and
synchronously; the job is an argument, not contents. Reuse is safe because the two shapes share
nothing (a POD passed by value vs. a refcounted interface behind `SharedRef`), so any stale use is
a compile error rather than a silent bug, and S0c lands as its own commit — the two meanings never
coexist in the tree.

**`IRenderContext` is an interface for ABI reasons, not for backend polymorphism.** `passes/`,
`fg/` and `scene/` contain zero D3D12 references — every piece of per-context state below is already
backend-agnostic and reaches the GPU through existing RHI interfaces, so a single implementation in
core serves any backend. It is pure-virtual only because bgl ships as a DLL and exporting a concrete
class with owning members across that boundary is fragile, and because it then fits the
`core::Ref` / `SharedRef` idiom every other RHI object uses — the same reasons `IGraphics` is shaped
that way.

There must never be a `RenderContext_d3d12`. A per-backend subclass would fork portable code for no
reason; the backend seam is already `ICommandQueue` / `ICommandList` one layer down.

---

## 1. What the survey changed about the spec

Ten findings from reading the code. Six make the work easier than the spec assumes, four make it
harder.

**Easier:**

* **`UploadManager` is already per-command-list, not per-device.** It is a member of `CommandList`
  ([CommandList_d3d12.h:158](../../libs/bgl/src/d3d12/cmd/CommandList_d3d12.h)), not of `Graphics`.
  A context that owns its command list gets its own upload ring for free — the spec's "own upload
  ring" bullet costs nothing. It is also not sized by `c_SwapchainImageCount`; it is a
  version-tagged chunk pool with a byte budget
  ([UploadManager.h:53-58](../../libs/bgl/src/d3d12/resource/UploadManager.h)).
* **`RenderTarget` already owns the per-frame ring.** `m_FrameIndex`, `m_FenceValues[2]`, and
  `m_CommandAllocator[2]` all live on the target
  ([RenderTarget_d3d12.h:92-98](../../libs/bgl/src/d3d12/RenderTarget_d3d12.h)), which `Graphics`
  reaches into as a `friend`. Half of "per-context state" is already sharded — only the recording
  and submission half is centralized.
* **`FrameGraph` already has multi-queue plumbing.** `RegisterQueue(name, queue, list)`,
  `PassDesc::queue` defaulting to `"main"`, and a `PassContext` carrying the resolved list/queue
  per pass ([FrameGraph.h:91](../../libs/bgl/src/fg/FrameGraph.h),
  [PassDesc.h:106](../../libs/bgl/src/fg/PassDesc.h)). Today exactly one queue is ever registered.
* **`Graphics` is defined entirely inside `Graphics_d3d12.cpp`** (`:138-309`) — there is no
  `Graphics_d3d12.h`. Nothing outside that translation unit can name the class, so it can be
  restructured freely without a ripple.
* **Everything a context owns is already portable.** `grep -rn "d3d12\|D3D12\|dxgi" libs/bgl/src/passes
  libs/bgl/src/fg libs/bgl/src/scene` returns nothing: the framegraph, all five pass objects, and
  `Scene`/`SceneView` are backend-agnostic and reach the GPU only through RHI interfaces. This is
  what makes a single core implementation viable, and it is why the context lives in core rather
  than behind the backend seam.
* **`core::slot_vector` pools of fixed capacity never reallocate — verified, not assumed.**
  `reset(slotCount)` resizes `m_Data`/`m_Meta` to `slotCount` up front and seeds `m_FreeIndices`
  with every index ([slot_vector.h:17-35](../../libs/core/include/core/containers/slot_vector.h)).
  `try_allocate_and_emplace` therefore always takes the free-list branch, and the `emplace_back`
  growth path is unreachable once `m_MaxSlots` is non-zero — exhaustion returns a null handle
  instead (`:68-95`). `ResourceManager` sizes every pool from `ResourceManagerDesc` at construction,
  so element addresses are stable for the manager's lifetime. **This is what makes `Get*(handle)` a
  lock-free read during recording**, and it converts S3's biggest unknown into a regression test.

**Harder:**

* **The pass objects are stateful and not re-entrant.** `ForwardPass` holds
  `std::array<MeshletKernel, c_PsoCount>` ([ForwardPass.h:69-72](../../libs/bgl/src/passes/ForwardPass.h));
  `CompactInstancesPass` holds three kernels plus two scratch `ComputeBuffer`s explicitly "not tied
  to the scene" ([CompactInstancesPass.h:54-61](../../libs/bgl/src/passes/CompactInstancesPass.h)).
  A `MeshletKernel`'s `Uniforms` CPU mirror is **mutated during Execute**. Two contexts cannot share
  one pass object. They must be per-context, which multiplies the scratch buffers by context count.
* **`FrameGraph::m_LastState` persists across `Reset()`** ([FrameGraph.h:176-178](../../libs/bgl/src/fg/FrameGraph.h))
  — it is cross-frame resource-state tracking for barrier generation. Two contexts with two
  framegraphs each independently believing they know a *shared* resource's last state will emit
  wrong barriers. This is the single most likely source of subtle corruption in the whole change,
  and it is why §6's decision gate matters.
* **`Graphics` is a `friend` of `RenderTarget_d3d12`** ([RenderTarget_d3d12.h:66](../../libs/bgl/src/d3d12/RenderTarget_d3d12.h))
  and reaches straight into `rt.m_FenceValues[]` and `rt.m_CommandAllocator[]` (`:92-98`). A
  portable `RenderContext` in core cannot do that. The per-frame fence/allocator ring has to become
  part of the `IRenderTarget` interface before the extraction can happen — bounded work, but it is
  a prerequisite of S1, not a detail of it.
* **`UploadManager` already encodes the queue in its version word and never compares it.**
  `MakeVersion` packs `QueueType` into bits 60-62 ([Version.h:11-36](../../libs/bgl/src/d3d12/cmd/Version.h)),
  but the reclaim predicate ([UploadManager.cpp:77-81](../../libs/bgl/src/d3d12/resource/UploadManager.cpp))
  compares only the fence value. With one timeline that is correct; with two it will recycle a
  chunk the other context's GPU work is still reading. A latent bug that this change activates.

---

## 2. What a context owns

Moved off `Graphics` onto `RenderContext`, with today's line numbers in `Graphics_d3d12.cpp`:

| State | Line | Why per-context |
|---|---|---|
| `m_CommandList` (+ its `UploadManager`) | `:261` | One recorder per list, between Open and Close |
| `m_CommandQueue` | `:260` | Independent fence timeline; see §5 on why not shared |
| `m_BootstrapAllocator` | `:265` | Used by `Resize` and to construct the list |
| `m_FrameActive`, `m_ActiveTarget` | `:267-270` | "One frame in flight" becomes per-context |
| `m_FrameGraph`, `m_DrawCount` | `:272-273` | Rebuilt per frame; `m_LastState` must not be shared |
| `m_PreparePresentPass`, `m_Forward`, `m_Skybox`, `m_CompactInstances`, `m_TransparentSort` | `:281-285` | Mutable `Uniforms` + scratch buffers, not re-entrant |
| `m_DebugBuffer`, `m_DebugReadbacks[2]`, `m_DebugReadbackPending[2]`, `m_DebugReadbackFence[2]` | `:300-307` | Ring is Graphics-wide today but filled per-target; the note at `:292-294` already flags that only the "main" queue binds it |
| `m_GpuAssertionHandler` | `:287` | Follows the ring |

Stays on `Graphics` (genuinely shared, and the entire advantage over a second device):

`m_Device`, `m_SlangGlobalSession`, `m_Opts`, the debug-layer ComPtrs, `m_ResourceManager`
(descriptor heaps + slot pools), and `Device::m_ShaderCache` (the on-disk program cache and
`ID3D12PipelineLibrary1`).

---

## 3. Target API

```cpp
// bgl/RenderJob.h — was RenderContext
struct RenderJob {
    core::SharedRef<ISceneView> view = nullptr;
    Camera                      camera;
    Viewport                    viewport;
};

// bgl/IRenderContext.h
struct RenderContextDesc {
    std::string debugName;
    uint32_t    maxDrawsPerFrame = 8;
};

class BGL_API IRenderContext : public core::Ref {
public:
    virtual void BeginFrame(const RenderTargetRef& target) = 0;
    virtual void Draw(const RenderJob& job)                = 0;
    virtual void EndFrame()                                = 0;

    void DrawFrame(const RenderTargetRef& target, const RenderJob& job);

    virtual void Resize(const RenderTargetRef& target, uint32_t w, uint32_t h) = 0;

    // Stage 6: split-phase. ScreenshotToMemory becomes Submit + wait + Resolve.
    virtual CaptureTicket                       SubmitCapture(const RenderTargetRef& target) = 0;
    virtual std::optional<assetlib::ImageData>  TryResolveCapture(CaptureTicket ticket)      = 0;
    virtual assetlib::ImageData                 ScreenshotToMemory(const RenderTargetRef&)   = 0;

    virtual void SetGpuAssertionHandler(IGpuAssertionHandler*) noexcept = 0;
    virtual void DiscardPendingGpuAssertions() noexcept                 = 0;
};
using RenderContextRef = core::SharedRef<IRenderContext>;
```

`IGraphics` gains `CreateRenderContext(const RenderContextDesc&)` and **keeps its existing
frame methods**, delegating to an implicit primary context created in its constructor. That
compatibility shim is what lets stages 1-3 land with zero call-site churn across ~30 bgl tests,
the editor, gamelib tests and the examples. It is removed in stage 7, not before.

---

## 4. Stages

Each stage builds, passes its gate, and is independently revertable. **The win lands at S4.**

### S0a — Retire the Metal backend

Delete `libs/bgl/src/metal/` and the `metal` / `macos-*` presets, after pushing the current state to
a `backup/metal-backend` branch so nothing is lost.

The reasoning is the author's: the iOS target that motivated it is no longer the direction, the
backend is incomplete (`Graphics_metal.cpp` stubs `CreateScene`, `CreateSceneView` and both
screenshot paths), and a major structural change is the moment to drop it rather than port it. Two
things found while surveying support that:

* **CI never builds it.** The `compile-macos` job runs `macos-clang-debug`, whose preset leaves
  `RENDERER_BACKEND` unset — it builds `core`, `assetlib`, `assetlib_cli`, `bgl_idlgen` and
  `bgl_objects` only ([ci.yml:95-101](../../.github/workflows/ci.yml)). The Metal backend has no
  automated verification at all, so carrying it through this change means porting code that nothing
  proves still works.
* **It would otherwise need the S1 prerequisite twice.** `RenderTarget_metal` would have to expose
  the same per-frame fence/allocator ring through `IRenderTarget` as its D3D12 counterpart, for a
  backend that cannot run the tests that would validate it.

Keep the `macos-clang-debug` CI job: it builds the backend-free subset and is the only thing keeping
`core`, `assetlib` and the RHI headers honest about portability — which §7 leans on for concurrency
testing.

* **Gate:** `just build` and `just test` green on Windows; the `compile-macos` CI job still passes;
  `backup/metal-backend` exists on the remote and is reachable from the PR description.

### S0b — Measurement first

Nothing below is verifiable without a frame-time number. Add a viewport frame-time readout to the
editor (rolling mean + max over ~120 frames, drawn in the viewport or the status bar) and a
`--frame-stats` dump of the same to the log on exit.

* **Gate:** with the folder-populate case reproduced by hand, the readout shows the stall the spec
  measured — ~20-30 ms typical, ~700 ms worst-case cold. If it does not reproduce, stop and
  re-measure before writing any of the rest.

### S0c — Rename `RenderContext` → `RenderJob`

Mechanical and isolated. `bgl/RenderContext.h` → `bgl/RenderJob.h`; update `IGraphics::Draw`, the
Metal and D3D12 backends, the editor's `RenderTargetWindow`/`MaterialPreviewWindow`/
`AssetThumbnailCache`, examples, and bgl tests.

* **Gate:** all presets build (`windows-vs2026-msvc-dx12-debug` and `windows-clang-dx12-debug`);
  `just test` fully green; `git diff` contains no behavioral change.

### S1 — Extract `RenderContext` into core

**Prerequisite:** move `RenderTarget`'s per-frame fence/allocator ring onto the `IRenderTarget`
interface, so core code can drive a frame without `friend` access to `RenderTarget_d3d12` (§1).

Then introduce a single `RenderContext` class in **core** — `libs/bgl/src/`, not the d3d12 TU —
holding the §2 state and reaching the GPU only through `IDevice`/`ICommandList`/`ICommandQueue`.
`Graphics` owns exactly one and forwards every frame method to it. No public API change, no second
context, no behavior change.

Highest-churn part of the whole plan and the lowest-risk, because the safety property is simply
"there is still one context". Take the pass objects and the debug-readback ring with it; the ring's
per-slot fence gating (`Graphics_d3d12.cpp:495-577`) transfers unchanged, since it was already
written for "slot filled by one party, consumed by another".

* **Gate:** `just test` green including golden images; `just run bgl_tests -- --gpu-validation`
  clean (this stage moves barrier-emitting state, so GPU validation is mandatory here, not
  optional); editor frame time from S0b unchanged within noise.

### S2 — Per-context queue and list

The context creates its own `CommandQueue`, `CommandList` and bootstrap allocator instead of
borrowing Graphics'. Register the context's queue with its `FrameGraph` under `"main"`. Fix the
`UploadManager` reclaim predicate to compare the queue identity encoded in the version word, not
just the fence value — the latent bug from §1.

Still exactly one context is created, so the fence timeline remains globally monotonic and nothing
downstream has to change yet.

* **Gate:** `just test` green; `--gpu-validation` clean; a targeted `UploadManager` unit test that
  two versions with equal fence values but different `QueueType` do not alias.

### S3 — `ResourceManager` multi-timeline safety

Three pieces, all still under one context so they can be reviewed in isolation:

1. **Pin pool stability with a test.** §1 establishes that fixed-capacity `slot_vector`s never
   reallocate, which is what licenses lock-free reads. That is currently a property of the
   implementation, not a guaranteed one — add a `core_tests` case asserting `data()` is unchanged
   across a full allocate/retire/reclaim cycle at capacity, so a future `slot_vector` change that
   introduces growth fails loudly here rather than silently in a renderer race.
2. **Shard `m_PendingDeletions` per context.** Each context keeps its own retirement list and sweeps
   its own. This removes the only multi-producer container in the design outright — the alternative,
   one shared list behind a lock, is contended by every context on every destroy for no benefit.
3. **Lock only slot allocation and retirement.** One mutex around `try_allocate_slot` /
   `retire_slot` / `release_slot`. Not around reads, not around the (now per-context) deletion lists.
4. **Replace the scalar deletion fence with a gate.** `PendingDeletion::fenceValue` becomes a small
   vector of `{queue, fenceValue}` — a snapshot, taken at destroy time, of every live context's next
   fence value. `CleanupExpiredResources` frees a slot when **every** recorded pair has completed.
   Each context publishes its last-completed value as an `std::atomic<uint64_t>`, so the sweep reads
   other contexts' progress without taking their locks. An idle context satisfies its component
   trivially (completed == last submitted), so a quiet thumbnail context cannot pin memory. Delete
   the one-arg `Destroy*` convenience overloads
   ([ResourceManager_d3d12.h:120-124](../../libs/bgl/src/d3d12/resource/ResourceManager_d3d12.h)) —
   they implicitly read `m_SubmissionQueue->GetNextFenceValue()` and hardcode the single queue.

**Why a mutex and not a lock-free free list.** The question is fair — the free list is a
`std::vector<uint32_t>` used as a stack ([slot_vector.h:68-95](../../libs/core/include/core/containers/slot_vector.h)),
and a Treiber stack threading the free indices through `m_Meta` with a tagged
`std::atomic<uint64_t>` head (index + ABA counter) would make it lock-free with no third-party
dependency. It is a real option, and pieces 2 and 4 above already remove the other shared structures
without any lock at all. But it should not be the starting point:

* **The contended region is a `pop_back` on a `uint32_t` vector** — tens of nanoseconds. The
  per-draw path (`Get*`) never touches it, so this lock is not on the hot path; it is taken on
  asset load and resource destroy.
* **Lock-free is exactly the code this project cannot verify.** §7 covers why: there is no
  ThreadSanitizer on Windows. An ABA bug in a hand-rolled Treiber stack is precisely the failure
  that survives every test in the suite and then corrupts a descriptor heap in front of a user.
* **The measurement decides.** If S4 shows contention on this mutex, the lock-free version is a
  contained, single-file change behind an unchanged `slot_vector` API — it can land later on
  evidence rather than now on speculation.

**On starvation.** Real but not a practical risk here, and worth being precise about: `std::mutex`
on Windows is SRWLOCK-based and explicitly *unfair* — it permits barging, so there is no FIFO
guarantee and a waiter can in principle be skipped repeatedly. What makes that harmless is the
shape of the workload, not the lock: the critical section is a few instructions, and the contenders
are a viewport context that touches it rarely and a thumbnail context that touches it in bursts. The
failure mode to actually watch for is **convoying** — the viewport blocking behind the thumbnail
context during a bulk asset load — which is bounded by the critical section, not by the size of the
load, precisely because the lock does not cover the uploads themselves. If S4's frame-time
distribution shows a tail that correlates with asset loads, that is the signal to revisit.

* **Gate:** `just test` green; `--gpu-validation` clean; a new `bgl_tests` case that creates,
  destroys and recreates enough resources to wrap a small pool, asserting no slot is reused before
  its gate completes; the `core_tests` pool-stability case from piece 1.

### S4 — Expose `IRenderContext`; second context; thumbnail cache moves — **the win**

Ship `IRenderContext` + `CreateRenderContext`. `IGraphics` keeps its frame methods against an
implicit primary context.

**Interim constraint: two contexts must not share a `Scene`.** That is the §6 decision, and it is
not made here. So `AssetThumbnailCache` creates its own context *and* its own `Scene`, and the
editor's `Renderer` grows a second worker thread that owns it. The cache's `DrainOne` stops using
`Renderer::Invoke` and posts to that thread instead.

This costs what the spec says a second device costs — env maps and thumbnail textures uploaded
twice — but it shares the device, the PSO/shader cache, the descriptor heaps and the resource
contexts, and it is enough to isolate the stall.

* **Gate — the measurement, and the point of the whole exercise:** with S0's readout, populate a
  cold folder while a viewport renders. Viewport mean frame time stays at vsync (~16 ms) and max
  stays under ~20 ms, against the ~700 ms baseline. Record both numbers in the PR.
* `just test` green; `--gpu-validation` clean; `editor_tests "[thumbnails]"` green with goldens
  matching.
* **New test:** two contexts drawing concurrently to two headless targets over two scenes, each
  golden-image compared. Use the `Readback_test.cpp:20-55` escape hatch (`As<GraphicsBase>()` →
  `GetDevice()` / `GetResourceManagerCpy()`) as the precedent for standing this up.

### S5 — **DECISION GATE**: shared-`Scene` policy

See §6. Nothing here is designed yet. Implementing the chosen option removes S4's duplicate
uploads and completes the spec's one-line test.

* **Gate:** the S4 measurement holds, *and* env maps/materials are uploaded once — assert by
  instrumenting `Scene::AddTextureAsset` call counts in the thumbnail test.

### S6 — Split-phase capture

`CaptureBackbuffer` (`Graphics_d3d12.cpp:896-956`) blocks the CPU twice: once on the producing
frame's fence, once on the copy's. Replace with a per-context readback ring modelled directly on
the debug-readback ring — fence-gated slot, pending flag, idempotent consume.

`SubmitCapture` records the copy and returns a `{slot, fence}` ticket. `TryResolveCapture` returns
`nullopt` until the fence passes, then maps and converts. `ScreenshotToMemory` stays as
Submit + wait + Resolve so every existing test keeps working unchanged.

`AssetThumbnailCache` then submits during one drain turn and resolves on a later one, so its
thread never blocks either.

* **Gate:** `just test` green; thumbnail goldens unchanged; thumbnail throughput (thumbnails/sec
  over a fixed folder) improves against the S4 number, or the stage is not worth keeping.

### S7 — Async upload handoff, and retire the shim

* Move the render-thread half of asset upload onto the thumbnail context. gamelib's
  `TexturePrefetch` already unfuses decode from upload
  ([AssetManager.h:32](../../libs/gamelib/include/gamelib/AssetManager.h)); this finishes the job so
  the viewport context never pays an upload for a thumbnail.
* **Revisit `c_WarmupFrames = 6`** ([AssetThumbnailCache.cpp:24-25](../../apps/editor/src/Thumbnails/AssetThumbnailCache.cpp)).
  It exists because the upload rings are `c_SwapchainImageCount` deep. With a dedicated context and
  a resolved capture it should drop; measure rather than assume, and keep the goldens honest.
* Remove `IGraphics::BeginFrame/Draw/EndFrame/Resize/Screenshot*`; callers use a context. Update the
  editor, examples, gamelib and bgl tests.

* **Gate:** `just test` green; `--gpu-validation` clean; `docs/rhi.md` threading section and a new
  `docs/render_contexts.md` updated; `docs/passes.md` checked for per-context scratch-buffer claims.

---

## 5. Why a queue per context, not one shared queue

Both are viable and the choice is reversible, but per-context is the default here.

A single `ID3D12CommandQueue` executes submitted lists **in submission order**. Sharing it keeps
`ResourceManager`'s scalar fence comparison valid and makes S3 nearly free — but it reinstates the
spec's own problem one layer down: the thumbnail's GPU work serializes ahead of the viewport's. At
~2 ms per thumbnail that is tolerable; during a cold upload burst it is not, and that burst is
exactly the 141-695 ms case the spec is trying to fix.

Two DIRECT queues let the hardware scheduler interleave, which is what the dropped two-device
experiment actually demonstrated. The price is precisely S3 — fence values stop being globally
comparable — which is bounded, reviewable work that has to happen for any N-context future anyway.

---

## 6. The open decision: sharing a `Scene` across contexts

Deferred by the spec, and deferred by this plan to the S5 gate. Sharing the `Scene` is the entire
advantage over a second device, and it is the one place a real concurrency policy is unavoidable.

The concrete problem has two halves. **CPU:** the authoring API (`AddStaticMesh`,
`SetSubmeshMaterial`, …) mutates `Scene`'s `RangeBuffer`/`EntryBuffer` contexts and dirty-block state
from the caller's thread with no synchronization. **GPU:** `Scene::Update(cmdList)` is recorded as
a framegraph pass ([Scene.cpp:340-346](../../libs/bgl/src/scene/Scene.cpp)) — two contexts sharing a
Scene would each record it, and each context's `FrameGraph::m_LastState` would track the shared
buffers' barrier state independently and wrongly. `ResourceManager::m_PendingTextureUploads` has
the same shape: appended by whichever context creates a texture, drained by whichever context records
`Scene::Update` first.

`Scene` already has the raw material for the epoch approach: `m_MaterialEpoch`
([Scene.h:217](../../libs/bgl/src/scene/Scene.h)) polled by `SceneView::m_SceneEpoch`
([SceneView.cpp:380-384](../../libs/bgl/src/scene/SceneView.cpp)), plus `slot_vector` generations
and per-buffer dirty-block tracking.

| | Latency | Extra memory | Complexity | Barrier correctness |
|---|---|---|---|---|
| **A. Single mutator + fence publication** | Reader sees new geometry ≥1 frame late | None | Medium | Owner alone tracks state; readers `InsertWaitForQueue` on the published upload fence |
| **B. Reader-writer lock over contexts** | Writer starves — recording holds the read lock for a whole frame | None | Low to write, high to live with | **Unsolved** — the lock orders CPU access, not GPU barriers |
| **C. Per-context snapshot / COW dirty state** | None | Duplicate upload bandwidth | High | Fragile — two contexts may write the same GPU range, benign only if byte-identical |
| **D. No sharing (S4's interim state)** | None | Env maps + textures duplicated per scene | None, already built | Trivially correct |

**Lean: A.** One context owns all `Scene` mutation and is the only one that records `Scene::Update`;
other contexts author through a queued request to the owner, and before drawing an epoch they have
not yet waited on, insert a GPU wait on the owner's published upload fence. It is the only option
that solves the barrier half rather than only the CPU half, and it needs no extra memory. Its real
risk is that the owner stalling now stalls its readers — which puts the owner on the viewport
context, the one that must never block, and makes S6 a prerequisite rather than a follow-up.

**B is not viable alone** — listed because it is the obvious first instinct and it does not address
the GPU ordering at all.

**D is the honest fallback.** If A measures badly, S4's state is already shipped and correct; the
cost is duplicated uploads, not a stall.

*This needs your call before S5 begins.*

---

## 7. Verifying concurrency on Windows / MSVC

The uncomfortable answer first: **ThreadSanitizer does not exist on Windows.** MSVC ships ASan
(`/fsanitize=address`) but no TSan, and LLVM's TSan supports Linux, macOS and FreeBSD only — the
`windows-clang-*` presets do not change that. Valgrind's Helgrind and DRD are Linux-only. So the
tool that would directly answer "is this data-race free" is unavailable on the primary platform,
which is a real constraint on the design and is why §S3 argues against hand-rolled lock-free code.

What is available, in descending order of value per unit of effort:

1. **Debug-only thread-affinity assertions — do this in S1, not later.** The contract is "one thread
   drives a context". Record the owning `std::thread::id` when a context is created and `gassert` it
   at the top of every entry point (`BeginFrame`, `Draw`, `EndFrame`, `Resize`, capture). This turns
   the invariant from a documented convention into a deterministic, zero-false-positive failure at
   the moment of violation. It catches the mistake this change actually invites — a client calling a
   context from the wrong thread — which no sanitizer would flag as a race anyway, because it is not
   one until it corrupts something.
2. **A dedicated stress test.** N contexts × M iterations hammering shared `ResourceManager`
   allocation and destruction concurrently, under the D3D12 debug layer with GPU validation, with
   randomized `std::this_thread::yield()` between operations to widen interleaving windows. Races
   here are probabilistic, so run it long in CI and treat an intermittent failure as a real defect,
   never as flake.
3. **TSan the portable pieces on the macOS runner.** The `compile-macos` CI job already builds
   `core`, `assetlib` and `bgl_objects` with clang. The pure-CPU concurrency logic — `slot_vector`
   allocation, the multi-timeline deletion gate — is backend-free and can be exercised there under
   `-fsanitize=thread` by a `core_tests` harness that drives it from several threads. **This is the
   strongest reason to keep the macOS CI job after S0a removes Metal**, and it is worth deliberately
   *placing* the deletion gate in a portable header so it falls on the testable side of the line.
   The honest limit: it covers the data structures, not `ResourceManager`'s D3D12 half.
4. **Application Verifier** (Windows SDK) for lock and handle misuse, and **`/analyze` with
   concurrency SAL** (`_Guarded_by_`, `_Requires_lock_held_`) to make the "this field is only
   touched under that mutex" rule machine-checked rather than a convention.

Intel Inspector does detect races on Windows and is the only tool here that competes with TSan, but
it is commercial; worth knowing it exists if the stress test ever points at something unreproducible.

**The takeaway for the design:** verification is weak enough on this platform that the plan should
prefer structures that are *correct by construction* over structures that are *fast and checked* —
which is what §S3's sharding (no shared container at all) and its preference for a boring mutex over
a Treiber stack are both buying.

---

## 8. Risks

* **Barrier state divergence across contexts** (§1, §6). The likeliest source of silent corruption.
  Mitigation: `--gpu-validation` is a gate on every stage from S1 on, not just at the end.
* **`slot_vector` growth would invalidate lock-free reads.** §1 verifies fixed-capacity pools never
  reallocate today, and S3 piece 1 pins it with a test — but it is an implementation property, not a
  contract of the container. If a future change introduces growth, per-draw handle dereference needs
  a lock and the cost model changes materially.
* **The editor viewport is not headlessly testable.** `RenderTargetWindow`'s constructor calls
  `CreateRenderTarget` with `winId()` and `headless = false` with no guard, so "does the viewport
  still get frames while thumbnails render" — the property this whole change exists to guarantee —
  cannot be pinned by a test. Either add a `headless` flag to `RenderTargetWindowDesc` during S0b,
  or accept that S4's gate is a manual measurement. **Recommend the flag**; a manual gate on the
  one property that matters will not survive contact with future changes.
* **Per-context pass objects multiply scratch GPU buffers** — `CompactInstancesPass` alone carries
  two `ComputeBuffer`s per context. Watch VRAM in the S4 measurement.
* **Someone will try to write `RenderContext_d3d12`.** The single core implementation is the point
  (see the naming note above); a per-backend subclass would fork portable code for no reason, and
  the backend seam already exists one layer down at `ICommandQueue` / `ICommandList`. Guard this in
  review.
* **S0a removes the only non-Windows backend.** After it, `docs/rhi.md`'s backend-agnosticism claims
  describe an aspiration with one implementation rather than a tested property. Keeping the
  `compile-macos` job is what stops the RHI headers quietly acquiring Windows assumptions — and §7
  depends on that job for the only ThreadSanitizer coverage available to this project.
* **`AssetThumbnailCache`'s second thread is editor-side complexity** the spec does not discuss.
  `Renderer` currently *is* the render thread; it becomes a thread pool with per-context affinity,
  and its destructor ordering (`MainWindow.cpp:159-173`) is already delicate.
