# Spec: multi-context bgl

A problem statement and rationale. Not a plan — no phases, no schedule. What is wrong, why it is
structural, and why the proposed shape fixes it.

## The problem

bgl is single-threaded by contract: exactly one thread may touch a `Graphics` and everything it
owns. The editor honours that with an actor (`apps/editor/src/Render/Renderer`) — one render
thread owns the device, and all work reaches it as queued closures. That fixed the original
stutter (GUI busyness can no longer starve the frame loop), but it exposed the next one:

**Every client of the device shares one submission stream, so one client's blocking work stalls
every other client.**

The measured case is the thumbnail cache. One thumbnail is:

```
BeginFrame  → WaitForFenceCPUBlocking     (ring-slot fence, Graphics_d3d12.cpp:592)
draw ×2
capture     → WaitForFenceCPUBlocking     (draw fence,      Graphics_d3d12.cpp:915)
            → submit copy
            → WaitForFenceCPUBlocking     (copy fence,      Graphics_d3d12.cpp:956)
            → map + convert
```

Warm (files in OS cache): draws 8–16 ms + capture 18–57 ms per thumbnail. Cold: 141–695 ms more
of texture/geometry upload ahead of the draws. All of it executes between viewport frames on the
one render thread, and the viewport misses vblanks for exactly that long — measured 20–30 ms
frames while a folder populates, 737 ms worst-case cold.

The GPU is nearly idle throughout. A 256×256 thumbnail is ~2 ms of GPU work. The stall is the
CPU sitting in fence waits, in front of other clients' frames.

## Why a queue cannot fix it

The editor already moved from immediate mode to command submission — `Renderer::Post` is the
submit API, the closures are the commands, the render thread is the runtime loop. The stall
survived that change, which is the proof of the general point:

**A queue does not shorten its commands; it only decides who waits.** Head-of-line blocking is a
property of a command's *contents*. A command that internally performs three cold fence waits
blocks the queue head for their sum, whether the queue lives in the editor or inside bgl, and
whether the commands are closures or typed structs.

So the fix is not a better queue in front of the device. It is either:

- **removing the waits** — split-phase operations (submit the capture copy now, map it frames
  later, when the fence is long signalled); or
- **giving the waits somewhere harmless to happen** — a second, independent submission stream,
  so a client's waits block only that client.

These are orthogonal and both eventually wanted. This spec is about the second.

## The idea: submission contexts

The unit of isolation is a **context**: own command list(s), own allocator ring, own fence
timeline, own upload ring, own frame-active state. A client — the viewport set, the thumbnail
cache, a future background baker — owns one context and drives it from one thread.

bgl does **not** become free-threaded. The invariant that makes it lock-free today — exactly one
thread dereferences a given object — is kept *per context*. The architecture is N single-threaded
actors over one device, not one thread-safe library. Locks appear only at the few points where
contexts genuinely meet (below), not around the hot path.

This is not speculative. The shape was proven in the editor this session at the coarsest possible
granularity: a second `Graphics` instance on its own thread — two of everything, zero shared
state, zero locks — and it isolated the stall completely. Its cost was the granularity: a second
device duplicates env maps, texture uploads and pool memory, and shares nothing. A context is the
same isolation drawn *inside* one device, so sharing survives.

## Why it fixes it

- **The waits stop mattering.** A context's `BeginFrame` ring wait and capture waits block only
  the thread that owns that context. The thumbnail context stalls 50 ms; the viewport context
  never observes it. Head-of-line blocking dies because there is no shared head.
- **The GPU interleaves the work anyway.** `ID3D12CommandQueue` submission is free-threaded, and
  the hardware scheduler interleaves work from multiple queues (or multiple lists on one queue).
  The thumbnail's ~2 ms of GPU work slots between viewport frames without displacing them —
  which the two-device experiment demonstrated end to end.
- **Cold-path uploads isolate too.** The 141–695 ms of texture/geometry upload rides the
  uploading context's ring and timeline. File I/O stays off render threads entirely (it already
  runs on a worker pool; that is unchanged).
- **The editor is already shaped for it.** Clients reach bgl only through `Renderer`. "The cache
  owns a private rendering context" is the structure the editor has now; whether that context is
  a second device (today's dropped experiment) or a context on the shared device (this spec) is
  invisible above the facade.

## Why it is structural today

Everything below assumes there is exactly one submission stream per device. Each is a reason
"just call bgl from two threads" corrupts state, and together they are the definition of what a
context has to own:

| Shared state | Single-stream assumption baked into it |
| --- | --- |
| `Graphics::m_CommandList` + bootstrap allocator | One recording at a time, ever |
| One direct `CommandQueue`, one fence timeline | "Done" is one monotonic value; every wait and every deferred free compares against it |
| `ResourceManager` deferred destruction | Frees are keyed to fence values of that one timeline; with N timelines, "safe to free" must hold on every timeline that referenced the resource |
| `ResourceManager` descriptor allocation | Slot allocation is unsynchronized |
| `UploadManager` rings | Sized per frame-in-flight of the one queue (`c_SwapchainImageCount = 2`); a slot is reused when *the* fence passes |
| `Graphics::m_FrameActive` | One frame in flight per *device* — `CaptureBackbuffer` throws if any frame is recording (Graphics_d3d12.cpp:901) |
| PSO / shader caches | Plain maps. (The D3D12 device itself is free-threaded — concurrent PSO creation is legal; only the cache is not) |
| `Scene` / `SceneView` pools | Mutated by whichever caller loads assets, read by draws, no ordering between the two |

The last row is the honest hard part. Command lists, timelines, rings and flags shard cleanly per
context; the scene's resource pools are the one place contexts *want* to share (that sharing is
the entire advantage over a second device), so they are the one place a real concurrency policy
is unavoidable — whether that is "one context owns mutation, others read with epoch/snapshot
consistency" or finer-grained. That decision is the heart of any future design work, and this
spec deliberately does not make it.

## Non-goals

- **Free-threaded API.** No "call anything from anywhere". Per-context affinity is the contract,
  same as today's per-device affinity.
- **Parallel recording within a context.** CPU submission is already cheap — GPU-driven culling
  and O(few) PSO buckets make `DrawFrame` milliseconds of CPU. Splitting one frame's recording
  across threads buys nothing here.
- **Replacing split-phase ops.** Contexts move waits; async removes them. Screenshot/readback
  should become submit/resolve pairs regardless (the debug-readback ring,
  Graphics_d3d12.cpp:495, is the in-repo precedent). A context whose commands never block is
  better than either alone.
- **Multi-GPU.** One adapter, one device.

## The one-line test

When this exists: the thumbnail cache constructs its context instead of a second device, its
50 ms of fence waits execute unmoved — and the viewport's frame-time overlay stays at 16 ms while
a cold folder populates, with env maps and materials uploaded once instead of twice.
