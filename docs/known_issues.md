# Known Issues — bugs that were fixed, and how to tell if one is back

One entry per bug that cost somebody a day and could plausibly return: what it looked like from the
outside, what actually caused it, what closed it, and **what to check first if the same symptom
appears again**. The point is that the second person to see a symptom recognises it in a minute
instead of re-deriving it from a stack trace.

An entry here is about a symptom, and it earns its place by being hard to diagnose rather than by
being recent. Delete one when the code it warns about is gone — when a backend is retired, so is its
entry.

Each entry states the gates that pin the fix. **If a gate is green and the symptom is back, the
entry's cause is not this one** — that is the fastest thing the entry can tell you, and it is why the
gates are named rather than described.

---

## `core_tests` hangs on one shard, at full CPU, with no failing case named

**Symptom.** `just test` never finishes: one `core_tests` shard sits for hours with the suite lock
held, and beside it a second `core_tests` process at 100% CPU. Sampled, the shard is in
`CrashLog_test.cpp` → `wait4`; the child it forked is in the crash handler, at
`crash_log_path()` → `localtime_r` → `tzsetwall_basic` → `notify_register_tz` →
`notify_register_check` → two frames inside libsystem_notify → `_dispatch_once_wait`. Which
shard, and whether at all, depends on the random test order: the suite is sharded across one
random order, and the case lands wherever the partition puts it.

**Cause.** The handler built its stamp with `localtime_r`. On macOS the first `localtime` in a
process initialises the time zone through libnotify under a `dispatch_once`, and that state does
not survive a `fork`: a child whose parent never called `localtime` waits on it forever. The crash
tests fork a child to crash on purpose, so an order in which nothing before them had touched the
time zone hung the child in the handler and the parent in `wait4`. The same call is not
async-signal-safe in any process: a crash on one thread while another holds the time-zone lock
deadlocks the handler with no log written.

**Fixed by** the handler stamping from `time()` and integer arithmetic alone, in local time from an
offset `install_crash_handlers` reads once, on the installing thread
([util.cpp](../libs/core/src/err/util.cpp), `crash_log_path`).

**Gates.** `just run core_tests -- "[crashlog]"`: the stamp is checked against the wall clock the
parent reads the ordinary way, and the crash tests install the handlers in the parent, so the
forked child calls no time function at all.

**If it comes back.** Read the signal path from `crash_signal_action` to the file name first:
nothing on it may call `localtime`, `gmtime`, `strftime` or `tzset`, and a new stamp, a new
timestamp in the log body, or a logger reached from the handler is where one would arrive. The
allocations the handler already makes (`std::ofstream`, `std::format`, cpptrace) are a different
hazard: they can deadlock on a crash inside `malloc`, never wait forever on the time zone.

---

## `assetlib_tests` hangs on one shard, at *no* CPU, inside a texture bake

**Symptom.** `just test` never finishes: one `assetlib_tests` shard sits for hours holding the suite
lock while every other shard passes. The discriminator against the `core_tests` hang above is the
CPU — that one spins at 100%, this one consumes **nothing**. Sampled minutes apart its consumed time
does not move (17s after 83 minutes) and its footprint stays near 6 MB. Sampled, the shard is in a
bake, and the bottom of the stack is the whole story:

```
MaterialBake_test.cpp → AssetStore::BakeMaterial → bakeMaterial
  → writeKTX2 → buildKtx → compressBasis (image_io.cpp)
    → ktxTexture2_CompressBasisEx → basisu::job_pool::~job_pool()
      → std::thread::join() → __ulock_wait
```

**Cause.** A lost wakeup in basisu's thread pool, vendored inside libktx and none of ours.
`job_pool::~job_pool` (`external/basisu/encoder/basisu_enc.cpp`) sets its kill flag and notifies
**without holding the pool's mutex**, then joins:

```cpp
m_kill_flag = true;        // std::atomic<bool>, but not under m_mutex
m_has_work.notify_all();   // notified without the lock
for (...) m_threads[i].join();
```

A worker waits on that condition variable under that mutex —
`m_has_work.wait(lock, [this]{ return m_kill_flag || m_queue.size(); })`. A worker that evaluates the
predicate false and is descheduled before it enqueues on the condition variable never receives the
notification, then sleeps on a flag that is already true; the destructor's `join()` never returns.
`std::atomic` removes the data race on the flag, not the lost wakeup: closing that window needs the
destructor to hold `m_mutex` while setting the flag, and it never takes it.

The window is a few instructions wide, so it takes load to land in — `just test` shards
`assetlib_tests` four ways and each shard asks for `hardware_concurrency()` basisu threads
(`bp.threadCount`, [image_io.cpp](../libs/assetlib/src/texture/image_io.cpp)), so a 12-core machine runs 48
workers and a thread descheduled inside that window stays there for a scheduler quantum rather than
nanoseconds.

**How rare it is, measured.** Sharding alone is *not* enough to bring it out: three consecutive
`just test assetlib_tests` runs on an otherwise idle machine all passed, and the two unsharded
`just run assetlib_tests` runs seen either side of the failure passed too. The one occurrence was a
`just test` while a second checkout was also working the machine. So do not expect to reproduce it
on demand, and do not read a clean run as evidence it is gone — the suite lock serialises *suites*
across checkouts, not builds, so a sibling checkout compiling beside your suite is load the lock
does not exclude.

**Fixed by** taking `m_mutex` around the kill flag before notifying, in
[cmake/ports/ktx/0007-basisu-job-pool-deadlock.patch](../cmake/ports/ktx/0007-basisu-job-pool-deadlock.patch).
It had to be a patch: nothing bernini owns ever holds the pool, which is built and destroyed inside
`ktxTexture2_CompressBasisEx`, so the only reachable seam is the dependency. vcpkg's `ktx` port
already applies six patches, so this is a seventh, carried by an overlay port under
[cmake/ports/ktx](../cmake/ports/ktx) that is otherwise vcpkg's port verbatim. It is a backport —
upstream basis_universal already takes the lock, and adds a one-second `wait_for` timeout on top.

Dropping `bp.threadCount` to 1 also removes it, since `job_pool` spawns no workers below two threads,
and was rejected: single-threaded UASTC encoding of a 4K mip chain is too slow to ship.

**Gates.** The build itself, and deliberately not a test. A race that survived three consecutive
sharded runs cannot be pinned by a case that passes either way — such a test buys false confidence
and would be worse than none. What pins the fix instead is that **vcpkg fails the build when a patch
does not apply**: bump `ktx` past a basisu that already carries the lock and the port stops
configuring, by name, which is the signal to delete the overlay rather than a silent revert.

Do not read `just run assetlib_tests -- "[ktx2][threading]"` as this gate. That case covers
`be6ad7ed`, which serialised Basis codec **init** behind `basisInitMutex` — a different phase whose
green says nothing here. The sampled frame was `image_io.cpp`'s `g_Ready` fast path, which takes no
lock at all, so init had long since succeeded and this hang was in pool teardown after an encode that
worked.

**If it comes back.** Check the overlay is still in the build before anything else: the port under
`cmake/ports/ktx` and its `overlay-ports` entry in `vcpkg-configuration.json` are both load-bearing,
and deleting either silently restores the upstream destructor. Then sample the wedged process before
killing it — `job_pool::~job_pool` under `ktxTexture2_CompressBasisEx` at flat CPU is this one, and a
stack anywhere else is not. Kill it promptly whatever the cause: it holds the machine-wide suite lock
and every other checkout queues behind it.

---

## SIGSEGV in libobjc while a Metal `Graphics` is torn down

**Symptom.** `editor_tests` on `macos-clang-metal-debug` exits 11 with no failing case named, roughly
one suite run in six, always after the last assertion has passed. The crash log
(`build/<preset>/bin/editor_tests_crash_<stamp>.log`) reads:

```
--- CRASH DETECTED (signal 11, faulting address 0x0000000000000000,
                    0x000000018fd5ec4c at /usr/lib/libobjc.A.dylib) ---
```

**The faulting address is `0` and the instruction is in libobjc**, below `-[AGX… dealloc]`, below
`bgl::Graphics::~Graphics`. Two different release chains reached it, so match on that shape rather
than on the frames: one drained an autorelease pool into a released device, the other released an
`MTL::Buffer` out of `~ResourceManager`.

**Cause.** Two defects, both in `CommandQueue::Flush`, both about a Metal object outliving what it
needs:

1. `MTL::CommandQueue::commandBuffer()` returns an **autoreleased** buffer, and `Flush` was the one
   `NewCommandBuffer` caller that scoped no pool around it. Every flush therefore parked a live
   command buffer in the pool `Graphics` held for the whole process — and that pool was declared
   *above* the device, so it drained after the device was released. A command buffer holds its
   queue, which holds the device.
2. `Flush` waited on the event its buffer signals, and that signal fires as the **GPU** passes it,
   with the driver still retiring the buffer and releasing what it held. Measured on an M-series
   machine: after `Flush` returned, 144 of 200 already-executed buffers were still not `Completed`.
   Every caller — `~RenderContext`, `Resize`, `SetRenderScale`, `WaitIdle` — then frees resources
   with `deferred = false`, whose precondition is that the GPU is idle for them
   ([rhi.md](rhi.md) § IResourceManager).

**Fixed by** `Flush` owning the pool its buffer drains into and ending on `waitUntilCompleted`, and
by `Graphics::m_Pool` being declared below the device. The two rules that came out of it are in
[`libs/bgl/CLAUDE.md`](../libs/bgl/CLAUDE.md) § bgl_metal, which is where to read them before writing
Metal code — not here.

**Gates.** `just run bgl_tests -- "[teardown]"`. Two cases, both deterministic where the crash is
not, and each failed before its fix: the queue's retain count must not scale with the number of
flushes (131 retains after 128 flushes, unfixed), and nothing executed before a `Flush` may still be
unretired when it returns (31 of 64, unfixed).

**If it comes back.** Run the gates first. If they pass, this entry's causes are excluded and the
following were already ruled out, so do not spend the day on them again:

- **Metal API validation is clean.** `METAL_DEVICE_WRAPPER_TYPE=1 MTL_DEBUG_LAYER=1` over the whole
  suite reports nothing, so it is not a resource freed while an in-flight command buffer references
  it, and not an over-release Metal tracks.
- **Nothing autoreleases without a pool** on the teardown path — twenty suite runs logged no
  `autoreleased with no pool in place`.
- **It is not the Qt thread hop.** `Renderer` builds *and* tears down its `Graphics` inside `Invoke`,
  so one thread pushes and drains every pool involved.
- **`slot_vector` cannot double-release.** `reclaim_slot` assigns `T()` and throws on a second
  reclaim, and a deferred free's gate covers the frame being recorded when it was retired.

**Reproducing it costs more than you expect.** Nothing smaller than the suite has ever reproduced it:
120 shard runs, 60 runs of the case that crashed, and a 30-vs-30 A/B against the unfixed `libbgl_extended` all
came back clean, as did Metal API validation. It appears to need the four shards running at once,
each holding a device — which is what `just test editor` does and a lone binary does not. Budget for
a rate near one suite run in twenty, and do not read a handful of clean runs as a fix.

---

## Every static mesh is missing on D3D12, and the cull says it dropped everything

**Symptom.** Nothing drawn through the static tier appears on Windows: a headless render is the clear
colour, and `[culling]` reports `tested 170, culled 170` beside a floor whose luma is 0. Metal is
unaffected. Four suites fail at once — `assetlib_tests` aside, every one that renders a mesh — which
looks like four unrelated breakages. What still draws is anything whose program is not
`StaticMesh.slang`: `AnyMesh` dispatches its meshlets without the group cull, so the
`[hashedalpha]` figures do not move and are no help in spotting this.

**Cause.** The static tier's amplification stage built its survivor mask with `InterlockedOr` into
the `CulledPayload` it dispatches with. An amplification payload is its own address space on D3D12,
and an atomic addressed into one does not land — every bit stayed clear, so the mesh stage was told
no group survived. Metal's payload is ordinary threadgroup memory, where the same code works, which
is why it shipped green twice. The mask is built in a `groupshared` array now and copied into the
payload once the barrier has passed.

**The second half.** With the mask populated the device was *removed* instead
(`DXGI_ERROR_DEVICE_HUNG`, TDR) from `CulledGroupIndex`, whose two loops counted to values read out
of payload memory. Both count to constants now — 32 bits in a mask word, 8 halvings over the 256
words a payload can hold. The preconditions that bound them were checked to hold while the hang
happened, so this is not a data fault: a trip count that is a payload read is the thing to avoid.

**Gates.** `just run bgl_tests -- "[culling],[pbr],[meshlet]"`. The first case compares a
culled render against an unculled one pixel for pixel, and the `[culling][view]` case pins the tested
and culled counts, so an all-zero mask fails both rather than quietly drawing less.

**If it comes back.** Check the gates first: if they are green, the mesh is missing for another
reason. If they fail with everything culled, look for an atomic or a loop bound that reads payload
memory — `grep -n "Interlocked" libs/bgl/shaders/src/lib/forward/mesh_stage.slang` should
find none addressing `gCulledPayload`. A shader-only change needs no C++ rebuild, so iterate on it
directly.

---

## `--gpu-validation` removes the device, in a case that passes without it

**Symptom.** `just run bgl_tests -- "[culling],[pbr],[meshlet]" --gpu-validation` dies with
`PIX detected a Device Removal` and `DXGI_ERROR_DEVICE_HUNG` in the log, followed by ten
`ID3D12GraphicsCommandList::*: This API cannot be called on a closed command list` as the frame graph
runs on past it. The stack is `CommandList::Barrier` under `FrameGraph::Execute`, in
`DirectionalLight_test.cpp`'s `[pbr]` metal case. The same 37 cases pass without the flag, and the
named case passes *with* the flag when it is the only case in the run — so it reads like an
interaction between cases, and it is not.

Any case that draws a static mesh through the renderer does it: `[capture]`, `[taa]`, `[resize]`,
`[motionvectors]`, `[surface]` and more. With the suite sharded, one removal is a TDR, which resets the
adapter, so every other shard dies with it and the failures look scattered.

**Cause.** The static tier's mesh shaders (`MSMain` and `MSDissolve` in
[StaticMesh.slang](../libs/bgl/shaders/src/programs/forward/StaticMesh.slang)) returned
early for a culled meshlet and then reached the backface cull's `GroupMemoryBarrierWithGroupSync`.
`visible` is uniform across the group in practice, but a barrier after a return the compiler cannot
prove uniform is undefined, and instrumented it deadlocks: the `Forward World` pass never finishes
and TDR removes the device. Uninstrumented it happens to work, which is why it survived. Neither
stage returns now; a culled meshlet's counts are zero, so its loops are empty and every thread
reaches the barrier.

**Why it looks order-dependent.** `SetEnableGPUBasedValidation` sets it on the *debug layer*, which
is the process's, not the `ID3D12Debug1` that asked: every device created afterwards is instrumented,
including one whose own controller never asked and one created after that controller was released.
Roughly half the suite's cases set `enableGPUValidationLayer` and half do not, so under the flag the
ones that do not are instrumented anyway — whenever one that does ran first. The metal case is one
that never asks, which is why it survives alone and hangs behind `[compute]`. Minimal repro, two
cases:

    bgl_tests.exe "Compute dispatch writes a bindless buffer,A metal is lit by a directional light" --gpu-validation

**Ruled out**, each by measurement: a stale driver pipeline library replayed into a validating device
(it hangs the same with the cache directory emptied); a second device in the process (two cases that
both decline validation pass under the flag); and the instrumented frame's cost, which was the
first explanation here: a single frame of one cube hung, and it stops hanging once the mesh shader
is edited alone -- a constant pixel shader still hangs, and so does clamping every loop in the mesh
stage to its constant maximum.

**Gates.** `bgl_tests.exe "[capture]" --gpu-validation` hangs the device within one frame
if the barrier is behind a return again, and the minimal repro above does too.
`bgpu::GpuContext::GpuValidationActive`
([GpuContext_d3d12.cpp](../libs/bgpu/src/d3d12/GpuContext_d3d12.cpp)) pins the scope half: it
answers for the process rather than reading the desc back, so a successor context cannot report
"off" while running instrumented and have its owners cache driver pipelines built without the
instrumentation.

**If it comes back.** Find the pass first: submit and wait after every pass in `FrameGraph::Execute`
and log its name, and the one that never returns is the one TDR killed -- fence waits on a removed
device return at once, so trust the log's removal timestamp over any "ok" printed after it. Then
look in that pass's shaders for a group barrier after a `return`, or a loop whose trip count is read
from payload memory (the entry above). Shaders are compiled from `bin/shaders/src` at run time, so
edit that copy and rerun one case to bisect without a rebuild; the next build overwrites it.

## Random patches of a mesh-stage draw are missing on Metal, differently every frame

**Symptom.** A pass whose amplification group gathers the work of several threads into one
payload -- a `groupshared` struct filled by `InterlockedAdd`-claimed slots, then
`DispatchMesh(count, 1, 1, payload)` after a barrier -- drops whole runs of its launches. The
terrain draw lost rows of patches; the holes moved from frame to frame and from run to run of the
same binary, and no validation layer said a word. The same code with every node drawn
unconditionally, or with no reads of a scene buffer, looked whole, which sent the diagnosis at the
buffer uploads first.

**Cause.** The payload gathered by many threads of one amplification group: with Slang's Metal
backend, what the mesh groups received was not always what the group had written when the count
said it had. Reads and uploads were fine throughout.

**Fixed by** one amplification group of one thread per launch, as the grass stage has always been
written: the thread decides and launches, and the payload is its own
([programs/forward/Terrain.slang](../libs/bgl/shaders/src/programs/forward/Terrain.slang)).

**Gates.** `just run bgl_tests -- "[terrain][render]"`: the field under the camera reads green in
its centre box, which a missing patch row broke on most runs. **Check first** whether the pass
gathers across threads into a payload; if it does, that is the cause, whatever the uploads look
like.

## `vkCreateShaderModule` rejects a program's SPIR-V on Vulkan: an argument's type, `PhysicalStorageBufferAddresses`

**Symptom.** With the debug layer on, building a pipeline logs two errors per program, every time a
renderer is made: spirv-val's `OpFunctionCall Argument <id> '…'s type does not match Function <id>
'…'s parameter type`, naming one of the buffer family's accessors (`RawBuffer_LoadTag`,
`EntryBuffer_Get`), and `SPIR-V Capability PhysicalStorageBufferAddresses was declared, but …
bufferDeviceAddress`. Only a debug build (`BERNINI_GPU_DEBUG`) shows it, the program still draws on
NVIDIA's driver, and `slangc` on the same file reproduces it only at `-O0`: at its default level it
inlines everything and the call is gone, while the engine links each module separately and keeps
the library's functions out of line.

**Cause.** A Slang 2026.7.1 code-generation bug. A function of the program that takes a struct read
from a buffer -- a `MeshInstance`, a `GrassDraw`, a `ToonShadingRig` -- and hands one of its
entries to an accessor that asserts on it (`dbg_assert(!entry.Null(), …)`) has that accessor emitted
taking the entry by a `PhysicalStorageBuffer` pointer, while every call passes a `Function`
pointer. It is not the entry's `Null()`, not the assert channel's atomics and not the optimization
level the engine asks for: each was ruled out with the program reduced to the one call chain.

**Fixed by** `[ForceInline]` on the program's own functions that take such a struct and call the
accessors: `SkinOf` and `HeadWorld` in
[EvaluateToonShadingRigs.slang](../libs/bgl/shaders/src/programs/toon/EvaluateToonShadingRigs.slang),
`SourceOf` in [Grass.slang](../libs/bgl/shaders/src/programs/forward/Grass.slang). Inlining the
accessors themselves instead moves the bug to the next accessor down and crashes `slangc` on two
other programs.

**Gates.** `just run bgl_tests -- "[swapchain],[grass]"` on `windows-clang-vulkan-debug`, then
`bgpu.log` holds no `spirv-val` error: every renderer builds the toon program, and the grass cases
build the grass one. **Check first** whether the program named passes a struct it read from a buffer
into a function of its own that calls an asserting accessor; if it does, inline that function.

## The build fails compiling a grass program: `dxc` reports `llvm::cast<X>() argument of incompatible type!`

**Symptom.** `just build` stops at `Compiling Slang Entry Point: Grass.slang [ASMain]` (or
`[MSMain]`) with `dxc 1.9: error: llvm::cast<X>() argument of incompatible type!` and nothing else:
no line, no Slang diagnostic. `slangc` with the build's own arguments (the `FAILED:` line of the log)
reproduces it at once, with or without `-g2`, and only for the D3D12 target, where Slang hands its
HLSL to DXC.

**Cause.** A DXC 1.9 crash on an amplification payload that carries a struct array. Two shapes of
it were found, both in the grass stage's payload, which hands the mesh groups the chunk's kept
clumps: storing a whole struct into the payload's array at an index known only at run time (the
amplification stage), and passing the payload itself to a function (the mesh stage) -- the second
crashes even once the array holds plain vectors. It is not the debug information, the groupshared
`InterlockedOr`, the stats counters, a default-constructed `GrassLook` or the clump's own reads: each
was ruled out by removing it from a copy of the program and compiling that alone.

**Fixed by** the payload carrying its clumps as flat arrays of vectors (`clumpPositionScale`,
`clumpNormal`, `clumpColorSeed`) written one element at a time, and the mesh stage passing a clump's
elements into `LaunchedClump` rather than the payload
([Grass.slang](../libs/bgl/shaders/src/programs/forward/Grass.slang)).

**Gates.** `just build`: the build compiles `Grass.slang`'s `ASMain` and `MSMain` through `slangc`
and DXC (`libs/bgl/shaders/CMakeLists.txt`), and fails on the crash. **Check first** whether the
change puts a struct into a payload array, or passes a payload or a struct read from one into a
function; if it does, flatten the struct into vectors or pass its members.
