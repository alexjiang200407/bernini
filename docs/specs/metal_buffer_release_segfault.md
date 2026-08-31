# A buffer released at Metal teardown still segfaults, rarely

`editor_tests` on `macos-clang-metal-debug` can still die with SIGSEGV at address 0 while a
`Graphics` is torn down — seen once in twenty `just test editor` runs. It is not the crash
[metal-teardown-segfault](../plans/metal-teardown-segfault.md) fixed: that one drained an
autoreleased command buffer into a released device, and its mechanism is gone and pinned by
`bgl_tests`' `[teardown]` case. This one is a different release chain into the same instruction.

## What it looks like

Same header as the old one — `signal 11, faulting address 0x0`, `0x18fd5ec4c` in libobjc — reached
from the resource manager rather than from a pool drain:

```
Graphics::~Graphics()                          libs/bgl/src/metal/Graphics_metal.cpp
  → SharedRef<IResourceManager>::~SharedRef()
  → ResourceManager::~ResourceManager()        libs/bgl/src/metal/resource/ResourceManager_metal.h
  → slot_vector<Buffer>::~slot_vector()
  → Buffer::~Buffer()  →  NS::SharedPtr<MTL::Buffer>::~SharedPtr()
  → -[AGXG15XFamilyBuffer dealloc] → -[AGXBuffer dealloc] → IOGPU → Metal → objc
  → SIGSEGV
```

Nothing here is a use of a released device: `m_Device` is declared below `m_ResourceManager`, so it
outlives every buffer the manager drops. The buffer's own release is its last, the GPU was idled by
`~RenderContext`, and `slot_vector` cannot release an element twice (`reclaim_slot` assigns `T()` and
throws on a second reclaim). What the driver is deallocating *behind* the buffer, and why that is
already gone, is the part nobody has established.

## What has been ruled out

- **A stray autorelease with no pool.** Twenty suite runs after the fix log no
  `autoreleased with no pool in place` warning, so nothing on the teardown path is leaking that way.
- **The Qt thread hop.** `Renderer` builds and tears down its `Graphics` inside `Invoke`, so one
  thread pushes and drains every pool involved.
- **The command-buffer chain.** That is what the plan above fixed, and it is a different stack.

## Reproducing it

Nothing smaller than the suite has reproduced it. Thirty runs of the shard that crashed
(`editor_tests --rng-seed 1230962760 --shard-count 4 --shard-index 3`) against `master`'s `libbgl` and
thirty against the fixed one both came back clean, as did sixty runs of the single case. It appears to
need the four shards running at once, each holding a device — which is what `just test editor` does
and a lone binary does not.

## Why it is not fixed here

The rate is too low to measure a fix against: twelve suite runs before the change crashed zero times
and twenty after crashed once, which distinguishes nothing. Somebody working on this wants a repro
that fails in minutes first — the four-process load above is the lead — and a Metal API validation run
(`METAL_DEVICE_WRAPPER_TYPE=1`) over it, which reports an over-release where a stack trace cannot.
