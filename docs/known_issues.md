# Known Issues — bugs that were fixed, and how to tell if one is back

One entry per bug that cost somebody a day and could plausibly return: what it looked like from the
outside, what actually caused it, what closed it, and **what to check first if the same symptom
appears again**. The point is that the second person to see a symptom recognises it in a minute
instead of re-deriving it from a stack trace.

This is not [`docs/specs/`](specs/), which describes problems we have decided *not* to solve, and not
[`docs/plans/`](plans/), which records the decisions behind one change. An entry here is about a
symptom, and it earns its place by being hard to diagnose rather than by being recent. Delete one
when the code it warns about is gone — when a backend is retired, so is its entry.

Each entry states the gates that pin the fix. **If a gate is green and the symptom is back, the
entry's cause is not this one** — that is the fastest thing the entry can tell you, and it is why the
gates are named rather than described.

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
[`libs/bgl_extended/CLAUDE.md`](../libs/bgl_extended/CLAUDE.md) § bgl_metal, which is where to read them before writing
Metal code — not here.

**Gates.** `just run bgl_extended_tests -- "[teardown]"`. Two cases, both deterministic where the crash is
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
