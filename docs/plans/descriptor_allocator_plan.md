# Descriptor allocator — implementation plan

Separates two things `bgl` still treats as one number: **which resource a handle names** and **where
a shader finds it**. A resource's slot index in the manager's pool *is* its index into the D3D12
shader-visible descriptor heap, and that equation is written down not only in the D3D12 backend but
in backend-agnostic code — `uniforms/`, `scene/`. This plan gives the descriptor heap its own
allocator and makes the ResourceManager own the mapping between the two.

This is a *plan*, not a mirror of code. When the work lands the durable parts belong in
[rhi.md](../rhi.md) and [bgl_api.md](../bgl_api.md).

**The load-bearing property: no shader changes and no public API changes.** A shader still reads a
`uint` out of a constant buffer and indexes a heap with it; `IGraphics`/`IScene` are untouched. What
changes is *who computes that uint*.

> **Rebased onto the Metal world.** This plan was first written when the two backends were D3D12 and
> WebGPU. WebGPU was deleted (#175–#180) and Metal landed (#213), so the branch was rebased onto
> master and the plan rewritten against it. The design decision in § 2 had to be retaken, because
> Metal cannot make the choice the original plan made. The task PRs merged before the rebase (#156,
> #158, #161, #164, #167) are not on the branch any more — they are preserved on the
> `pre-rebase/descriptor-allocator` tag and the remaining tasks cherry-pick from it rather than
> reimplementing. #168 was closed because its backend no longer exists. § 3 is the work that remains.

---

## 1. What the survey found

### There are two paths, and only one has been fixed

A handle becomes a number the GPU can use in two quite different situations, and master now treats
them differently:

| | Written | Resolved by |
|---|---|---|
| **Stored in GPU memory** — a material's texture | once, by the CPU | `ResolveDescriptor` — **done** |
| **Bound through a constant buffer** — everything in `Uniforms` and the scene buffers | per frame | the identity — **still open** |

The first was solved by the Metal port, with a seam that looks very like the one this plan set out to
add:

```cpp
[[nodiscard]] virtual DescriptorHandle
ResolveDescriptor(const TextureHandle& handle) const noexcept = 0;
```
`resource/ResourceManager.h:194`, whose contract is already written down: *"What that has to be is the
backend's business: a descriptor-heap index on D3D12, the native resource id on Metal."* Both
backends implement it and `Scene.cpp:878,940` calls it.

An earlier draft of this plan concluded that `GetBindlessIndex` must therefore not be added, because it
would give one contract two names. **That was wrong, and D2 is where it was caught.** The two rows
above are two *different* contracts that happen to coincide on D3D12:

| | D3D12 | Metal |
|---|---|---|
| stored in GPU memory | heap index | native `gpuResourceID` — nothing can patch it later |
| constant buffer | heap index | **slot index**, which the dispatch rewrite looks up and replaces |

One function cannot answer both on Metal. Routing `Uniforms` through `ResolveDescriptor` would write a
native id into the cbuffer, and `MapUniformHandlesToGpuAddresses` would then read its low four bytes
as a slot index and dereference a garbage pool entry. So the two seams stay separate, and the
identical answers on D3D12 are a coincidence of that backend, not evidence they are one thing.

### Where the identity still lives

Eight call sites, all in backend-agnostic code, all constructing a descriptor straight from a slot:

| Site | What it does |
|---|---|
| `uniforms/Uniforms.h:193, 206` | `operator=(BufferHandle)` → `DescriptorHandle(handle.slot)` |
| `uniforms/Uniforms.h:221` | `operator=(SamplerHandle)` → `DescriptorHandle(handle.idx)` |
| `uniforms/Uniforms.h:235, 249` | `operator=(TextureHandle)` / `(TextureAssetHandle)` |
| `scene/PackedBuffer.h:249`, `scene/RangeBuffer.h:117`, `scene/EntryBuffer.h:257` | `GetDescriptorHandle()` |

They are all reachable from the constructor that encodes the assumption:

```cpp
explicit DescriptorHandle(core::slot_handle slot) : DescriptorHandle(slot.index) {}
```
`uniforms/DescriptorHandle.h:20`

On the D3D12 side the identity is now stated once, and says so plainly:

```cpp
ResolveDescriptor(const TextureHandle& handle) const noexcept override
{
    // The shader indexes the descriptor heap with it, so the slot is already the descriptor.
    return DescriptorHandle(handle.slot);
}
```
`d3d12/resource/ResourceManager_d3d12.h:152`

That comment is the whole plan in one line: it is true today, and D4 is what makes it false.

### Metal reintroduced the reversal WebGPU had

The original survey's sharpest finding was that WebGPU, having no heap, had to decode D3D12's
addressing backwards — `GetBufferBindingBySlotIndex`. That code went with WebGPU. Metal then arrived
and built the same thing:

```cpp
// D3D12 writes a bindless handle's slot index into the cbuffer and a directly-indexed heap
// resolves it in-shader; Metal has no such heap, so at dispatch each handle field is rewritten
// to the native value the emitted MSL dereferences [...]
MappedUniform MapUniformHandlesToGpuAddresses(...)
```
`metal/cmd/CommandList_metal.cpp:31`, reading through `GetBufferBySlotIndex` /
`GetTextureBySlotIndex` / `GetSamplerBySlotIndex`.

The argument for this plan is therefore stronger than when it was written: the reversal is not an
accident of one port, it is what the identity costs *every* backend without a heap, and it has now
been paid twice.

### What the identity still costs D3D12

Unchanged since the first survey, and all still present:

- **A second texture pool exists only to protect the heap.** `m_Textures` holds RTV/DSV-only
  textures so they never consume a bindless slot (`ResourceManager_d3d12.h:263-269`).
- **`TextureHandle::usage` is the pool discriminator.** Two pools means two index spaces, and `usage`
  is what picks one (`ResourceManager_d3d12.cpp:164, 242`). Caller-supplied data selecting an index
  space: wrong bits read the wrong pool at the same index and return a different texture.
- **Buffers and textures share a pool because they share a heap** —
  `using CbvSrvUavSlot = std::variant<Buffer, Texture>` (`ResourceManager_d3d12.h:18`).
- **Pool size *is* heap size.** `maxCbvSrvUavs` sizes both the slot vector and the heap
  (`ResourceManager_d3d12.cpp:11, 29`).
- **Generations are dropped.** `DescriptorHandle(slot.index)` discards `slot.generation`, so a stale
  handle written into a uniform block cannot be detected.

---

## 2. Design

### Two seams, one per question

`IResourceManager` carries both, and which one a call site wants is decided by where the number ends
up, not by what it names:

```cpp
// stored in GPU memory -- written once by the CPU, dereferenced as it stands
[[nodiscard]] virtual DescriptorHandle ResolveDescriptor(const TextureHandle&) const noexcept = 0;

// bound through a constant buffer -- the backend may still rewrite it at record time
[[nodiscard]] virtual uint32_t GetBindlessIndex(BufferHandle)  const noexcept = 0;
[[nodiscard]] virtual uint32_t GetBindlessIndex(TextureHandle) const noexcept = 0;
[[nodiscard]] virtual uint32_t GetBindlessIndex(SamplerHandle) const noexcept = 0;
```

On D3D12 both return the heap index, because the shader indexes the heap directly in either case. On
Metal they differ: `ResolveDescriptor` returns the native `gpuResourceID`, `GetBindlessIndex` returns
the pool slot that `MapUniformHandlesToGpuAddresses` looks up and replaces at dispatch. Metal's
behaviour is therefore unchanged by this plan — it keeps finding a slot index where it already expects
one.

Naming them apart is what stops a later reader "unifying" them: the D3D12 implementations are
identical today and will still be identical after D4, which makes them look redundant right up until
the moment a heapless backend proves they are not.

### Resolve eagerly, or patch at flush — retaken

`Uniforms::operator=` is where a handle becomes a number and it has no ResourceManager: it is built
from a pipeline and its `AccessorBase` carries only `(m_Data, offset, node)`. Two ways out, and the
first plan's answer no longer holds:

1. **Resolve eagerly.** `Uniforms` holds a `ResourceManagerRef` and `operator=` calls
   `ResolveDescriptor`. One virtual call per assignment.
2. **Resolve at record time.** `operator=` writes the slot and the backend patches the block when it
   is flushed.

The original plan chose (1) and rejected (2) as *"charging every D3D12 draw for a per-flush layout
walk"*. Since then Metal has implemented (2) — and **must** keep it, for a reason the first plan could
not have known: the walk is also where Metal collects the resources to declare resident
(`result.resident`, `CommandList_metal.cpp:60-75`). A residency list cannot be assembled at assignment
time, because nothing there knows which encoder will use the block.

**Still take (1), for D3D12 only.** It stays the smaller change, it keeps D3D12's flush a straight
`memcpy`, and — decisively — the machinery (2) would need on D3D12 does not exist: `GetHandleOffsets`,
`HandleSlot` and `HandleKind` are declared only in `metal/pipeline/*` and `metal/cmd/`, so choosing
(2) for D3D12 means building that reflection a second time to solve a problem that is not about
performance.

The two backends therefore resolve at different moments, which is exactly what a seam is for. Write
it down where the seam is declared, or the next reader will "fix" the asymmetry.

Consequence to accept: `ResolveDescriptor` is called from scene-update code. Reads in the manager are
already documented as lockless (fixed-capacity pools, storage never moves), so this lookup follows the
same rule and takes no lock.

### The allocator

A `DescriptorAllocator` owns each heap and hands out indices:

```cpp
uint32_t Allocate();          // throws when the heap is full
void     Free(uint32_t idx);  // returns it to the free list
```

`Texture` and `Buffer` already carry a `D3D12_CPU_DESCRIPTOR_HANDLE`; they gain the index that
produced it. Deferred destruction frees the descriptor on the same gate that reclaims the slot — the
descriptor must outlive in-flight work exactly as the resource does, which is what `RetireDeferred`
already sequences.

### What falls out

- **One texture pool.** `m_Textures` and the texture half of `m_CbvSrvUavSlots` merge.
- **`TextureHandle::usage` stops being a discriminator.** It keeps its descriptive meaning.
- **`std::variant<Buffer, Texture>` goes away**, for `slot_vector<Buffer>` and `slot_vector<Texture>`.
- **Pool size and heap size decouple.** `maxCbvSrvUavs` splits into a resource count and a descriptor
  count, with separate reasons.

---

## 3. Staging

Each step builds and passes on its own; the identity survives until D4 removes it, so nothing is
half-migrated at a commit boundary.

* **D1 — `DescriptorAllocator`, unused.** The class plus its tests: allocate to exhaustion, free and
  reallocate, and that a freed index is not handed out twice before `Free`. No wiring.
  **`bgl_tests` is no longer D3D12-only** — it builds with `RENDERER_BACKEND_METAL` on macOS from one
  unfiltered glob (`libs/bgl/CMakeLists.txt:175`), so a test that includes `<d3d12.h>` breaks the
  macOS build. This is what made the pre-rebase #156 unlandable. The test must be compiled only for
  the D3D12 backend.
  *Gate:* new unit tests in `bgl_tests`; **and `just build` green on both a D3D12 and a Metal preset**.
* **D2 — `GetBindlessIndex` on both backends, returning what the identity returns today.** D3D12
  returns `handle.slot.index`; Metal returns the same, which is what its dispatch rewrite already
  expects. A pure seam: every call site can migrate to it before the number behind it changes.
  *Gate:* `bgl_tests` green on both backends; no golden image moves. `BindlessIndex_test` drives the
  seam through `IResourceManager` rather than a backend type, so both implementations are covered.
* **D3 — move the constant-buffer call sites onto the seam.** `Uniforms` gains its
  `ResourceManagerRef`; the eight sites in § 1 stop constructing `DescriptorHandle` from a slot.
  `slot_handle::operator bool` becomes explicit, which is what makes the migration checkable —
  a handle can no longer promote to an integer, so anything left behind fails to compile. Scene.cpp
  keeps `ResolveDescriptor`: it is the other path, and it was migrated already.
  *Gate:* golden images bit-identical on **both** backends (the numbers have not changed yet, so any
  diff is a bug).
* **D4 — D3D12 allocates descriptors properly.** `CreateTexture`/`CreateStructBuffer` take an index
  from the allocator instead of using the slot index; destruction frees it on the deferred gate. This
  is the step where the two numbers diverge, and where the comment at
  `ResourceManager_d3d12.h:155` stops being true.
  *Gate:* golden images within tolerance; `just run bgl_tests -- --gpu-validation`, because a mis-freed
  descriptor is exactly what GPU-based validation catches and nothing else does. Metal unaffected —
  worth re-running to prove it.
* **D5 — collapse the pools.** `m_Textures` merges into the texture pool, the variant goes,
  `TextureHandle::usage` stops selecting an index space, and `maxCbvSrvUavs` splits.
  *Gate:* as D4, plus a test that an RTV-only texture and an SRV texture can hold the same slot index
  without colliding — the failure the old design prevented structurally and this one must prevent
  deliberately.

There is no WebGPU step. The old D6 removed `GetBufferBindingBySlotIndex`; that code left with the
backend.

---

## 4. What this does not do

- **Bindless is not removed.** Shaders still index a heap. This changes where the index comes from,
  not what a shader does with it.
- **Metal is not changed.** It implements D2 trivially and keeps its dispatch rewrite. If a later
  change wants Metal to stop reading slot indices out of cbuffers, that is a separate argument about
  residency, not about descriptors.
- **Generation checking is not added.** D3 makes it *possible* — the manager sees a full `slot_handle`
  at the point of translation — but validating there costs a branch per assignment and belongs in its
  own change with its own argument.
- **Sampler heap is untouched.** Samplers have their own small heap and their own pool, and no second
  pool grew around them; the sampler overload exists for symmetry.
- **No FrameGraph or barrier changes.** Resource lifetime and layout tracking key off handles, not
  descriptor indices.

## 5. Risk

The one that matters is **D4 landing silently wrong**. Once slot and descriptor diverge, an off-by-one
or a use-after-free reads a *valid* descriptor for the wrong resource — which renders something
plausible rather than crashing. That is why D4's gate is GPU validation and not just goldens: a
wrong-but-live descriptor can pass a tolerance-based image compare, and the debug layer alone does not
see it.

The cheap mitigation, worth doing in D4 rather than after: have the debug build write a known sentinel
into every freed descriptor, so a stale read lands on something visibly wrong instead of on whatever
took the slot.

The second risk is now **asymmetric backends**. From D2 on, `ResolveDescriptor` means different things
on D3D12 and Metal, and only D3D12's goldens can catch a mistake in D3D12's meaning. Every gate from
D2 onward names both backends for that reason: this repo is developed on macOS, where the D3D12 path
does not build, so a D3D12-only regression is invisible until CI or a Windows run.
