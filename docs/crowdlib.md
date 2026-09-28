# crowdlib — compute on a queue of its own, beside the renderer

The crowd simulation's library (`libs/crowdlib`, namespace `crowd`). Every item under `ROADMAP.md`
§ Crowd Simulation & Pathfinding is GPU work, and none of it belongs to the renderer's frame, so it
lives here: on the device the application's `bgpu::GpuContext` owns, compiled through that
context's Slang sessions, and submitted to a queue the renderer does not know about. Today it holds
the foundation and nothing more — one kernel, its buffer and its readback on the async queue.

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

* **It links `bgpu`, never `bgl_extended`.** The RHI stays hidden in the renderer, because it
  assumes the GPU-driven bar and not every renderer clears it ([bgpu.md](bgpu.md) § Design
  Choices). So this library reaches the device through `<bgpu/d3d12/native_device.h>` and
  `<bgpu/metal/native_device.h>` and drives each API itself, from `src/d3d12/` and `src/metal/`. It
  re-implements the small slice of an RHI it needs — a queue, a fence, a pipeline, two buffers — and
  knowingly: when the crowd work multiplies that slice, sharing the renderer's RHI or keeping two
  copies is a decision to make again.
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
  signature is two root parameters — root constants and a root UAV — with no descriptor heap. There
  is no shader cache of its own.
* **Built as the renderer is.** It shares the context's process-wide state, so
  `BERNINI_RENDERER_LIBRARY_TYPE` decides its kind as it does `bgpu`'s
  ([core_process.md § Linkage](core_process.md#linkage)), and `CROWD_API` marks its one export. It
  is in the build-tree package; the editor does not link it.

## Interface Index

| Symbol | File | Role |
|---|---|---|
| `HashFillJob`, `HashFillDesc` | [include/crowdlib/HashFillJob.h](../libs/crowdlib/include/crowdlib/HashFillJob.h) | The async queue, its kernel and its readback: `Submit`, the two fences, `Wait`, `GetReadback` |
| `CreateHashFillJob` | same | Builds all of it on a context's device |
| `HashFillReference` | same | The CPU half of the kernel, what element `i` must read back as |
| `CompileKernel` | [src/KernelCode.h](../libs/crowdlib/src/KernelCode.h) | A kernel through the context's sessions, with its reflected bindings |
| `crowd.CSHashFill` | [shaders/src/crowd/CSHashFill.slang](../libs/crowdlib/shaders/src/crowd/CSHashFill.slang) | The kernel |

## Threading & Synchronization

* **One thread**, like the renderer. A job compiles on the thread that creates it, which may create
  that thread's Slang session, so creation keeps `ReleaseSlangSessions`'s precondition; it holds no
  `slang::` object afterwards.
* **One submission in flight.** `Submit` while `InFlight()` throws, since the readback it would
  overwrite is the one the caller has not read yet; so does `GetReadback` before a result exists.
* **Teardown drains.** The job waits for its own last submission before it drops its context
  reference ([bgpu.md](bgpu.md) § Threading & Synchronization). It never waits on the renderer's
  queue, and the renderer never waits on its.

## Verification

`crowdlib_tests` `[render]`: the readback equals `HashFillReference` element for element, over a
count that is not a multiple of the kernel's group, and a second seed replaces the first result; the
job runs its queue while a renderer on the same context draws a cube, each polled and neither
waiting; and a job destroyed mid-flight lets its context go, so the next context can be created.
