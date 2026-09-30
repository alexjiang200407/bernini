# crowdlib — compute on a queue of its own, beside the renderer

The crowd simulation's library (`libs/crowdlib`, namespace `crowd`). Every item under `ROADMAP.md`
§ Crowd Simulation & Pathfinding is GPU work, and none of it belongs to the renderer's frame, so it
lives here: on the device the application's `bgpu::GpuContext` owns, compiled through that
context's Slang sessions, and submitted to a queue the renderer does not know about. Today it holds
the foundation — one kernel, its buffer and its readback on the async queue — and the crowd's public
interface, `crowd::ICrowd`, which only a test fake implements so far (§ The crowd interface).

```cpp
auto context  = bgpu::CreateGpuContext(ctxDesc);
auto graphics = bgl::CreateGraphics(context, gfxOpts);            // one owner
auto job      = crowd::CreateHashFillJob(context, { .count = 4096 });  // another

job->Submit(seed);                    // returns at once
while (running)
{
	graphics->DrawFrame(target, renderJob);
	if (!job->InFlight())             // polled, never waited on
		Use(job->GetReadback()), job->Submit(++seed);
}
```

## Design Choices

* **It links `bgpu`, never the renderer.** `bgpu` carries the RHI the renderer is built on
  ([bgpu.md](bgpu.md) § Design Choices), so a device, a compute queue, pipelines and the buffer
  family of this library's own are one `bgpu::CreateDevice` away, with nothing of `bgl` in the
  process. The hash-fill job drives each API itself, through `<bgpu/d3d12/native_device.h>` and
  `<bgpu/metal/native_device.h>`, from `src/d3d12/` and `src/metal/`: the slice of an RHI it wrote
  before `bgpu` carried one -- a queue, a fence, a pipeline, two buffers.
* **The async queue is a second queue, on both backends.** On D3D12 it is a
  `D3D12_COMMAND_LIST_TYPE_COMPUTE` queue with a fence. Metal has no compute-typed queue; its form of
  the same thing is a second `MTLCommandQueue`, whose command buffers the GPU may run concurrently
  with every other queue's, with a shared event as its fence. Nothing on either queue waits on the
  other, and the result reaches the CPU by a readback, never through a buffer the renderer reads.
* **Submit, then poll.** `Submit` records the kernel and the copy into the readback, signals the
  fence and returns. `GetCompletedFence` reads it without blocking, and `GetReadback` is legal once
  the fence has passed the submission. One submission is in flight at a time, since there is one
  readback. Caller misuse throws `std::runtime_error`; nothing here ends the process for it.
* **Kernels compile through the context's sessions.** `crowd.CSHashFill` is staged under
  `./shaders/src/crowd/` beside the executable, like every engine module, and loaded with
  `GpuContext::LoadModule`: DXIL on D3D12, MSL on Metal, compiled at runtime. On D3D12 the build also
  compiles it to DXIL once, as validation. Where each parameter is bound is read from reflection, not
  written into the shader: Metal counts a constant buffer and a structured buffer in one
  `[[buffer(N)]]` space, so a `register(b0)` and a `register(u0)` collide there. The D3D12 root
  signature is two root parameters — root constants and a root UAV — with no descriptor heap.
* **The kernel is cached in the context's program cache.** `LoadKernel` keys it under the owner
  tag `crowd`, and on a hit decodes the code and the two bindings without reaching a Slang session;
  on a miss it compiles and stores. It skips the Slang front-end only: crowdlib keeps no driver
  pipeline library, so Metal's MSL compile is paid every run ([shader_cache.md](shader_cache.md)).
* **Built as the renderer is.** It shares the context's process-wide state, so
  `BERNINI_RENDERER_LIBRARY_TYPE` decides its kind as it does `bgpu`'s
  ([core_process.md § Linkage](core_process.md#linkage)), and `CROWD_API` marks its one export. It
  is in the build-tree package; the editor does not link it.

## The crowd interface

`ICrowd` (`include/crowdlib/ICrowd.h`) is what a game drives the crowd through: a **crowd** holds
**groups**, and a group holds **agents**. The headers are the source of truth for the calls and what
each refuses; what follows is why it is shaped as it is.

* **Groups, never agents.** A game orders a group — a goal, a facing and a formation, all on the
  ground plane as world (x, z) — and reads back per-group aggregates (`GroupReport`). No call names
  an agent, writes one or reads one: per-agent CPU traffic is what `ROADMAP.md` § Guiding
  Constraints rules out. A group has one `AgentType`, its kinematics; a mixed force is several groups.
* **The method is position-based**, GPU Zen 3 ch. 13 (Weiss, "Real-Time Simulation of Massive
  Crowds"): agents are discs; each tick an agent blends its velocity toward its formation slot at
  its type's `preferredSpeed` times its group's `pace`, predicts a position, and the solver projects
  the constraints over it — overlap, weighted by `mass`; obstacles; anticipated collisions within
  the avoidance horizon; the links that hold a formation at its `spacing` — before the velocity is
  clamped to `maxSpeed`. The constraints and their order are the crowd's. A game tunes them
  (`SolverDesc`) and cannot add its own, which would need a GPU plug-in model nothing asks for yet.
* **Obstacles are segments**, replaced whole by `SetObstacles` and applied at the next `Step`. They
  keep agents out, but an agent plans straight toward its slot, so it presses against a wall rather
  than walking round it until a planner — the roadmap's flow fields — replaces the straight line.
* **Agents change group only with their group.** `SplitGroup` detaches the rear of a formation into
  a new group under a copy of its orders, and `MergeGroup` joins one group to the rear of another of
  the same type and releases it. There is no per-agent reassignment, for the reason above.
* **Commands take effect at the next `Step`, and the tick is fixed.** A command changes what the
  crowd *will* simulate, so the CPU's answers (`HasGroup`, `GetAgentCount`) are immediate while the
  reports trail by the ticks in flight. Every `Step` advances `CrowdDesc::tickSeconds`, never a
  frame's delta, so the same commands at the same ticks can replay the same simulation — which the
  interface keeps possible and does not yet promise.
* **A handle is refused from the call that releases it**, not from the tick that applies the
  release, and a reused slot never revalidates an old handle: `GroupHandle` is a generation-checked
  `core::slot_handle`, as `bgl`'s instance handles are.
* **Capacities are fixed at creation.** `maxAgents` and `maxGroups` size the GPU state once, so a
  command that would exceed one throws rather than growing it.
* **Plain C++ types, not IDL.** The descriptions carry validation rules and `std::vector`s; the
  GPU records they are packed into belong to the implementation, and go through the IDL with it.
* **Each backend implements `ICrowd` directly**, as `bgl_extended`'s backends implement the RHI's
  interfaces. There is no `CreateCrowd` yet: it arrives with the first backend, since a factory with
  nothing to create would only fail at link time.
* **The contract is a test suite.** `tests/src/Crowd_test.cpp` runs every case against each factory
  in `CrowdFactories`; a backend joins by adding its own. The cases tagged `[fake]` need what only
  the fake (`tests/src/FakeCrowd.h`) can promise: a tick held in flight, or a group reported standing
  at its goal the tick it is ordered there.

## Threading & Synchronization

* **One thread**, like the renderer, for a job and a crowd alike. A job compiles on the thread that creates it, which may create
  that thread's Slang session, so creation keeps `ReleaseSlangSessions`'s precondition; it holds no
  `slang::` object afterwards.
* **A job has one submission in flight.** `Submit` while `InFlight()` throws, since the readback it would
  overwrite is the one the caller has not read yet; so does `GetReadback` before a result exists.
* **Teardown drains.** The job waits for its own last submission before it drops its context
  reference ([bgpu.md](bgpu.md) § Threading & Synchronization), and a crowd waits for its ticks in
  flight (`ICrowd.h`). Neither waits on the renderer's queue, and the renderer never waits on theirs.
  A crowd allows `CrowdDesc::maxTicksInFlight` ticks at once, and `Step` past them throws.

## Verification

`crowdlib_tests` `[render]`: the readback equals `HashFillReference` element for element, over a
count that is not a multiple of the kernel's group, and a second seed replaces the first result; the
job runs its queue while a renderer on the same context draws a cube, each polled and neither
waiting; and a job destroyed mid-flight lets its context go, so the next context can be created.
`[shadercache]`: the kernel stored once cold, loaded on a fresh context with no entry rewritten, and
a torn entry compiled again. `[crowd]`: `ICrowd`'s contract, against the fake — a group absent from
every report until a tick that includes it completes, handles refused once released and after their
slot is reused, capacities and invalid descriptions refused (agent types, the solver, pace,
obstacles), a split and a merge conserving agents
and refusing to empty a group or mix types, and `Step` refused past `maxTicksInFlight`.

`examples/bgl_async_compute` is the same shape as a program: a cube drawn every frame while the
kernel runs on the async queue, the fence logged before and after each draw, and every readback's
checksum against the CPU's. `--frames N` exits non-zero on a mismatch or no readback at all, and
`--headless` draws offscreen.
