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
> master and the plan rewritten against it. Two things changed materially: half the seam this plan
> proposed **already exists** under the name `ResolveDescriptor`, and the design decision in § 2 had
> to be retaken, because Metal cannot make the choice the original plan made. The task PRs merged
> before the rebase (#156, #158, #161, #164, #167) are not on the branch any more; #168 was closed
> because its backend no longer exists. § 3 is the work that remains.

---

## 1. What the survey found

### There are two paths, and only one has been fixed

A handle becomes a number the GPU can use in two quite different situations, and master now treats
them differently:

| | Written | Resolved by |
|---|---|---|
| **Stored in GPU memory** — a material's texture | once, by the CPU | `ResolveDescriptor` — **done** |
| **Bound through a constant buffer** — everything in `Uniforms` and the scene buffers | per frame | the identity — **still open** |

The first was solved by the Metal port, and its seam is exactly the one this plan set out to add:

```cpp
[[nodiscard]] virtual DescriptorHandle
ResolveDescriptor(const TextureHandle& handle) const noexcept = 0;
```
`resource/ResourceManager.h:194`, whose contract is already written down: *"What that has to be is the
backend's business: a descriptor-heap index on D3D12, the native resource id on Metal."* Both
backends implement it and `Scene.cpp:878,940` calls it.

**The two paths are not one contract.** An earlier draft read the opposite — that widening
`ResolveDescriptor` would cover both. Implementing it disproved that. They ask different questions:

| | Question | D3D12 | Metal |
|---|---|---|---|
| Stored in GPU memory | what does the GPU dereference? | descriptor index | native `MTLResourceID` |
| Bound through a constant buffer | what does the *encoder* look the resource up by? | descriptor index | **pool slot** |

They coincide on D3D12, which is why one seam looked sufficient. On Metal they cannot: the cbuffer
field is rewritten at dispatch, and the rewrite finds the resource by pool slot
(`GetBufferBySlotIndex` and friends) — hand it a native id and there is nothing to look up.
`ResolveDescriptor` therefore stays as it is, serving the stored-in-memory path only.

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

### The constant-buffer index travels on the handle

A handle already names a resource. It now also carries what a shader needs to reach it:

```cpp
struct TextureHandle
{
    core::slot_handle slot;           // which resource
    TextureUsage      usage;
    uint32_t          bindlessIndex;  // what a shader indexes with
};
```

and the same field on `BufferHandle`, `SamplerHandle` and `TextureAssetHandle`. The **resource
manager stamps it at creation**, which is the one moment a backend is already in the loop: D3D12
writes the index its allocator handed out, Metal writes the pool slot its dispatch rewrite looks the
resource up by. `Uniforms::operator=` then writes `handle.bindlessIndex` and asks nobody anything.

Two things fall out. The mirror needs no resource manager and no backend-specific subclass — it stays
a value type built from a pipeline. And the two numbers become separable per *resource* rather than
per *call site*, so D4 changes one line in `CreateTexture` instead of every place a handle is written.

**Rejected: give `Uniforms` a `ResourceManagerRef`** (#229). It works, but it makes the
backend-agnostic mirror depend on the manager to answer a question the manager is not the authority
on — Metal's answer needs no manager at all.

**Rejected: split `Uniforms` into `IUniforms` plus a backend implementation** (#231, first attempt).
It moves the coupling into backend code, but it makes the mirror polymorphic and therefore
ref-counted, for a hook whose answer is a property of the handle rather than of the mirror writing
it. The index is the same number wherever it is written; asking the *writer* was the wrong question.

### Resolve eagerly, or patch at flush — retaken

Two ways to get the right number into the block, and the first plan's answer no longer holds:

1. **Resolve eagerly.** The number is decided before `operator=` runs and the mirror writes it.
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

Metal keeps patching at dispatch and is unaffected by any of this: the value it finds in the block is
the pool slot either way.

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
* **D2 — the handle carries its bindless index.** `BufferHandle`, `SamplerHandle`, `TextureHandle`
  and `TextureAssetHandle` gain the field; both resource managers stamp it with the slot index, which
  is what the mirror wrote before; `Uniforms::operator=` reads it. Nothing the GPU sees changes, and
  the two numbers become separable per resource.
  *Gate:* `bgl_tests` green on both backends, including a handle whose index and slot are
  deliberately different — the two are equal everywhere else, so nothing else can tell them apart.
* **D3 — move the scene buffers onto the field too.** `PackedBuffer.h`, `RangeBuffer.h` and
  `EntryBuffer.h` stop constructing `DescriptorHandle` from a slot. The
  `DescriptorHandle(core::slot_handle)` constructor is deleted, which is what makes the migration
  checkable — anything left behind fails to compile.
  *Gate:* golden images bit-identical on **both** backends (the numbers have not changed yet, so any
  diff is a bug).
* **D4 — D3D12 allocates descriptors properly.** `CreateTexture`/`CreateStructBuffer` stamp the
  handle with an index from the allocator instead of the slot index; destruction frees it on the
  deferred gate. This
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
