# metal-teardown-segfault — implementation plan

## Context

`editor_tests` on `macos-clang-metal-debug` dies with SIGSEGV at address 0 roughly one run in six,
always after the last assertion, always while a `Graphics` is being torn down on the editor's render
thread. The frames are stable across captures a day apart: draining
`Graphics::m_Pool` runs `-[AGXG15XFamilyCommandBuffer dealloc]`, which releases the queue, which
purges the device — a device the `DeviceRef` beside that pool had already released.

The command buffer sitting in that pool comes from `CommandQueue::Flush()`. `NewCommandBuffer()`
returns an **autoreleased** buffer, and `Flush` is the one caller that scopes no pool around it:
`CommandList::Open` retains it and scopes `m_ScopePool`, `RenderTarget::PresentToLayer` opens a local
pool. So every `Flush` — one per `WaitIdle`, and one more from `~RenderContext` — parks a live
command buffer in the process-lifetime pool that `Graphics` holds, and the last one parked there is
still holding the queue when the pool finally drains.

The intermittency is the driver's: whether the pool's release is the *last* reference on the queue
depends on when the driver retires a completed buffer.

Fixing that left a second, rarer crash on the same instruction, reached from `~ResourceManager`
releasing an `MTL::Buffer` rather than from a pool drain — and behind it a second defect.
`CommandQueue::Flush` waits on the event its buffer signals, and that signal fires as the *GPU*
passes it, with the driver still retiring the buffer and dropping what it held. Measured: after
`Flush` returned, **144 of 200** already-executed command buffers were still not `Completed`. Every
caller of `Flush` — `~RenderContext`, `Resize`, `SetRenderScale`, `WaitIdle` — then frees resources
with `deferred = false`, whose documented precondition is "only safe when the GPU is idle for that
resource" ([docs/rhi.md](../rhi.md) § IResourceManager). So teardown was releasing resources into a
driver that had not finished with them. That `editor_tests` is the only suite seen to
crash follows from the same count — the editor flushes on every `Resize`, `SetRenderScale` and
`MainWindow`'s idle, which its thumbnail rendering does constantly, so far more buffers are sitting
in the pool when it drains than in a `bgl_tests` case that brings a device up and puts it down again.

## Decisions

- **ADR-1 — `CommandQueue::Flush()` scopes its own autorelease pool.** The buffer is committed before
  the pool drains, so the driver owns it for the rest of its flight; nothing of it escapes the call.
  This is the root cause and the convention every other `NewCommandBuffer()` caller already follows.
  *Rejected: retaining the buffer in an `NS::SharedPtr` instead, because a retain fixes the lifetime
  of that one object while leaving the autoreleased entry — and every other stray — in the pool.*
- **ADR-2 — `Graphics::m_Pool` is declared so it drains before the device it protects.** A
  catch-all pool that outlives the device turns any stray autorelease into a use-after-free at exit,
  which is worse than having no pool at all. Ordering it below `m_ResourceManager` makes the drain
  land after `~RenderContext` has idled the GPU and before the resource manager and device are
  released. *Rejected: deleting `m_Pool` outright, because a stray autorelease on the frame path
  would then leak with only an objc console warning, and no evidence says there is none.*
- **ADR-4 — `Flush` waits for the buffer to be retired, not just for its event.**
  `waitUntilCompleted` after the fence wait, which Metal's submission order extends to everything
  committed before it. That is what "the GPU is idle for that resource" has to mean for the
  immediate frees every caller does next. *Rejected: leaving the fence wait alone and making the
  callers wait, because `Flush` is the one place that knows which buffer to wait on, and four
  callers would each restate the rule.*
- **ADR-3 — the thread hop is ruled out, not worked around.** `Renderer` builds *and* tears down its
  `Graphics` inside `Invoke`, so the pool is created and drained on the same render thread. No
  pool is moved between threads and nothing is added to guard against it.

## Non-goals

- `Graphics_d3d12` — the autorelease mechanism does not exist there, and whether it idles the GPU
  before releasing the device is a separate question this does not open.
- Removing the process-lifetime `Graphics::m_Pool`, or giving `BeginFrame`/`EndFrame` a pool of
  their own. Both are the same argument as ADR-2 and want their own evidence.

## Acceptance

- `just run bgl_tests -- "[teardown]"` — the queue's retain count must not scale with the number of
  `Flush` calls. This is the gate, because it is deterministic: unfixed it reports 131 retains after
  128 flushes, one per flush.
- `just run bgl_tests -- "[teardown]"` — two cases, both deterministic where the crash is not, and
  both failing before their fix: the queue's retain count must not scale with the number of `Flush`
  calls (131 retains after 128 flushes, unfixed), and no buffer executed before a `Flush` may still
  be unretired when it returns (31 of 64, unfixed).
- `just test editor` runs, as corroboration only. The crash itself was never reproducible on this
  machine — 12 suite runs before any fix, 120 shard runs, 60 runs of the case that crashed and a
  30-vs-30 A/B all came back clean, and Metal API validation over the whole suite reports nothing.
  So no run count here can show the crash is gone; what the gates above show is that both mechanisms
  behind it are. 25 further suite passes after the second fix are clean, which is corroboration at
  best — the crash ran at roughly one pass in twenty.

## Commits

1. `docs(plans): plan the Metal teardown segfault fix` — this file.
2. `fix(bgl): scope the autorelease pool CommandQueue::Flush leaks into` — ADR-1 and ADR-2, plus the
   retain-count case and the `libs/bgl/CLAUDE.md` rule. Gate: `just run bgl_tests -- "[teardown]"`.
3. `fix(bgl): wait for the flushed command buffer to retire, not for its event` — ADR-4 and the
   second `[teardown]` case. Gate: the same tag, plus `just test`.
