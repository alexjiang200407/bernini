# crowdlib — compute on a queue of its own, beside the renderer

The crowd simulation's library (`libs/crowdlib`, namespace `crowd`). Every item under `ROADMAP.md`
§ Crowd Simulation & Pathfinding is GPU work, and none of it belongs to the renderer's frame, so it
lives here: on the device the application's `bgpu::GpuContext` owns, compiled through that
context's Slang sessions, and submitted to a queue the renderer does not know about. Today it holds
the foundation — one kernel, its buffer and its readback on the async queue — and the crowd:
`crowd::ICrowd`, created by `crowd::CreateCrowd` on `bgpu`'s RHI, whose agents walk to their
formation slots but do not yet keep out of each other's way (§ The crowd interface).

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
  ([core_process.md § Linkage](core_process.md#linkage)), and `CROWD_API` marks its two exports, `CreateHashFillJob` and `CreateCrowd`. It
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
  **Only the velocity planning exists today**: no constraint is projected, so agents pass through
  each other and through obstacles, and `SolverDesc`'s iterations and stiffnesses are validated but
  change nothing.
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
* **The crowd's IDL is its own.** The records its kernels share with the CPU — an agent, a group, a
  range of one tick's agents and where it comes from in the last, a tick's parameters, a report
  row, a debug record — are Slang modules under `shaders/src/crowd/idl/`, imported by the kernels as
  `crowd.idl.<Name>` and mirrored into C++ as `crowd::idl` by `bgpu_idlgen` ([idlgen.md](idlgen.md)).
  The debug record is public, so its module is generated with `--public` into the committed
  `include/crowdlib/debug/AgentReadback.h`, `crowd::debug`, and has no private twin. Not the
  renderer's tree: its mirrors belong to `bgl`, which crowdlib does not link.
* **A formation has one CPU reference.** `SlotPosition` (`src/formation.h`) is where each slot of
  a group stands — ranks front to back, files from the facing's left, the block centred on the goal
  and each rank across the facing — and the kernels compute the same function, as
  `HashFillReference` is `CSHashFill`'s CPU half. It is internal: a game orders a formation and
  reads its report, and never needs a slot's position.
* **One per-agent read, for seeing the crowd.** `ReadDebugAgents` hands back every agent's position
  and facing and each group's range of them, as the last completed tick left them. It is in every
  build and off unless `CrowdDesc::debugAgentReadback` asks for it, so a crowd that does not pays no
  copy. It is a method rather than an interface of its own because it keeps no state of its own:
  its copies ride each tick and are reused on the crowd's tick ring. Debug data that did keep its
  own state would earn a separate interface.
* **One crowd, on `bgpu`'s RHI** (`src/Crowd.cpp`). `CreateCrowd` gives it a device, a resource
  manager and a compute queue of its own on the application's context, as `bgpu`'s compute-only test
  does, so the same code runs on D3D12 and Metal and crowdlib holds no crowd code per backend. Its
  CPU half is `CrowdPlan` (`src/CrowdPlan.h`): the handles, every refusal, and the layout each tick
  uploads, tested on the CPU alone.
* **A tick moves the layout by O(groups) records.** Agents are stored group by group, each group's
  in slot order, so a split is a cut and a merge an append. `CrowdPlan` keeps each group's agents
  as runs of the previous tick's buffer (or spawned), and each `Step` uploads a `Group` record per
  group and an `AgentRange` per run; the step kernel (`crowd.CSStep`) finds each agent's range by a
  binary search, copies it from the previous buffer or spawns it in its slot, plans its velocity and
  moves it, writing the other of two agent buffers. There is no per-agent upload, ever.
* **Movement.** An agent heads for its slot (`crowd.formation`, the Slang half of `SlotPosition`) at
  its group's speed, slowing to `distance / tickSeconds` so it stops in the slot rather than passing
  it; that is blended with its last velocity by `velocityInertia` and capped at `maxSpeed`. It faces
  its velocity above `c_WalkingSpeedShare` of its group's speed, and its orders' front below it.
* **Reports are reduced in a fixed order.** `crowd.CSReduce` runs one thread group per group: each
  thread sums a fixed stride of its agents and the sums are halved in a fixed order, so the same
  agents give the same bits. No atomics: D3D12 has no float ones, and their order is the
  scheduler's. `meanFacing` is the orders' front when the agents' facings cancel.
* **A ring of ticks, never a wait.** Tick `t` records into slot `t % (maxTicksInFlight + 1)`: its
  command list, and readbacks of the group sums and (with the debug readback on) every agent. The
  extra slot is the last completed tick's, which reads return while `maxTicksInFlight` newer ticks
  run, so what a read returns outlives the next `Step`. `GetCompletedTick` polls the queue's fence; nothing but `Wait`
  and teardown blocks.
* **The contract is a test suite.** `tests/src/Crowd_test.cpp` runs every case against each factory
  in `CrowdFactories`: the fake and the GPU crowd. The cases tagged `[fake]` need what only the fake
  (`tests/src/FakeCrowd.h`) can promise: a tick held in flight, or a group reported standing at its
  goal the tick it is ordered there. The fake keeps its own validation rather than sharing
  `CrowdPlan`'s, so the suite checks two implementations, not one twice.

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
a torn entry compiled again. `[crowd]`: `ICrowd`'s contract, against the fake and the GPU crowd — a group absent from
every report until a tick that includes it completes, handles refused once released and after their
slot is reused, capacities and invalid descriptions refused (agent types, the solver, pace,
obstacles), a split and a merge conserving agents
and refusing to empty a group or mix types, `Step` refused past `maxTicksInFlight`, and the debug
readback refused unless asked for and holding every agent in its slot on its group's first tick.
`[formation]` pins `SlotPosition`'s layout, and `[idl]` the records' round trip through a buffer.
`[crowdplan]`: each command's layout — a spawn, a split's rear, a merge's append, a destroy's
close-up — and a randomized run of commands against a model that follows every agent by identity.
`[movement]`, on the GPU through the debug readback: a group walks to new orders and stands in its
slots, at its group's speed and facing its way, never past `maxSpeed`; inertia keeps its share of a
turned velocity; a standing group faces its front; a split, a merge and a destroy move no agent
further than a tick's walk; reports arrive by polling with ticks in flight; and the same commands
give the same bits twice. On Metal the suite also runs clean under
`METAL_DEVICE_WRAPPER_TYPE=1 MTL_SHADER_VALIDATION=1`.

`examples/bgl_async_compute` is the same shape as a program: a cube drawn every frame while the
kernel runs on the async queue, the fence logged before and after each draw, and every readback's
checksum against the CPU's. `--frames N` exits non-zero on a mismatch or no readback at all, and
`--headless` draws offscreen.

`examples/bgl_crowd` draws the crowd: two infantry and two cavalry groups march across a field,
one box per agent from the debug readback, while the crowd steps a tick a frame on its own queue.
At tick 60 a group splits off its rear under new orders, and at tick 150 one cavalry group merges
into the other; every group's report is logged every 60 ticks. `--frames N` exits non-zero unless
every group's mean stands within one spacing of its goal by then (450 is enough), `--headless`
draws offscreen, and `--screenshot <png>` writes the last frame drawn, which is how an agent looks
at it.
