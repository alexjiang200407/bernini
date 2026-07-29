# Descriptor allocator — implementation plan

Separates two things `bgl` currently treats as one number: **which resource a handle names** and
**where a shader finds it**. Today a resource's slot index in the manager's pool *is* its index into
the D3D12 shader-visible descriptor heap, and that equation is written down not in the D3D12 backend
but in backend-agnostic code — `uniforms/`, `scene/`, `Scene.cpp`. This plan gives the descriptor
heap its own allocator and makes the ResourceManager own the mapping between the two.

This is a *plan*, not a mirror of code. When the work lands the durable parts belong in
[rhi.md](../rhi.md) and [bgl_api.md](../bgl_api.md).

**The load-bearing property: no shader changes and no public API changes.** A shader still reads a
`uint` out of a constant buffer and indexes a heap with it; `IGraphics`/`IScene` are untouched. What
changes is *who computes that uint*.

---

## 1. What the survey found

### The identity, and where it is established

The CBV/SRV/UAV pool is sized to the heap, deliberately:

```cpp
m_CbvSrvUavSlots(desc.maxCbvSrvUavs)             // ResourceManager_d3d12.cpp:11
heapDesc.NumDescriptors = desc.maxCbvSrvUavs;    // ResourceManager_d3d12.cpp:29
```

So slot *i* is descriptor *i*, and the conversion is a constructor that drops everything else:

```cpp
explicit DescriptorHandle(core::slot_handle slot) : DescriptorHandle(slot.index) {}
```
`uniforms/DescriptorHandle.h:15`

That is a good trade in isolation — no map, no indirection, and `ResourceDescriptorHeap[i]` lands on
the right descriptor by construction.

### Where it is *consumed* — the actual finding

Not in `src/d3d12/`. The call sites that turn a slot into a shader-visible index are all in code that
is supposed to be backend-agnostic:

| Site | What it does |
|---|---|
| `uniforms/Uniforms.h:194, 207` | `operator=(BufferHandle)` → `DescriptorHandle(handle.slot)` |
| `uniforms/Uniforms.h:222` | `operator=(SamplerHandle)` → `DescriptorHandle(handle.idx)` |
| `uniforms/Uniforms.h:236, 250` | `operator=(TextureHandle)` / `(TextureAssetHandle)` |
| `scene/PackedBuffer.h:249`, `scene/RangeBuffer.h:117`, `scene/EntryBuffer.h:257` | `GetDescriptorHandle()` |
| `scene/Scene.cpp:880, 939, 944` | material and texture routing |

None of them asks a backend anything. They assume D3D12's addressing scheme and write it into a
constant buffer.

**This survey undercounted: there were twelve.** The two `gDebug` auto-binds in
`CommandList_d3d12.cpp` construct via declaration syntax (`DescriptorHandle handle(slot)`), which
the expression-grep above missed — legitimate uses of the identity inside the backend, but still
call sites the D3 migration had to move. Worse, deleting `DescriptorHandle(slot_handle)` did *not*
surface them as compile errors: `slot_handle`'s implicit `operator bool` bound them to the
`uint32_t` overload, silently pointing every kernel's GPU-assert buffer at descriptor 1. The
`[gpu-assert]` suite caught it at runtime. `operator bool` is `explicit` now (D3), so the
delete-and-recompile check is airtight for anything that comes later.

### What that assumption already costs

**A second texture pool exists only to protect the heap.** A texture no shader samples must not
consume a descriptor in a heap sized for shader-visible resources, so it goes somewhere else:

```cpp
// RTV/DSV-only textures (no SRV): kept out of the shader-visible pool so they
// never consume a bindless descriptor slot.
core::slot_vector<Texture> m_Textures;   // ResourceManager_d3d12.h:262
```

**And `TextureHandle::usage` became the pool discriminator.** Two pools means two independent index
spaces, so the index alone is ambiguous. `usage` is what disambiguates — its only three uses in the
manager are exactly that (`ResourceManager_d3d12.cpp:389`, `:600`, `:676`). It is *caller-supplied
data selecting an index space*: a handle constructed with the wrong bits reads the wrong pool at the
same index and silently returns a different texture. `TextureHandle::From(TextureAssetHandle)`
hardcodes `kSRV` for that reason.

**Buffers and textures share a pool because they share a heap.**

```cpp
using CbvSrvUavSlot = std::variant<Buffer, Texture>;   // ResourceManager_d3d12.h:18
```

The pool's shape is dictated by the descriptor heap, not by the resources in it.

**The WebGPU backend has to run the identity backwards.** WebGPU has no descriptor heap, so it must
recover a resource from the raw index that `Uniforms` wrote:

```cpp
// Resolves the raw slot index a Uniforms handle write records (DescriptorHandle stores the
// index alone, without a generation) to its buffer, for bind-group assembly at dispatch.
const wgpu::Buffer& GetBufferBindingBySlotIndex(uint32_t slotIndex) const noexcept;
```
`webgpu/resource/ResourceManager_wgpu.h:98`

`CollectHandleBindings` (`webgpu/cmd/CommandList_wgpu.cpp:43`) `memcpy`s a `uint32_t` out of the
uniform bytes and looks it up. A backend with no heap is decoding another backend's heap addressing.

**Generations are dropped.** `DescriptorHandle(slot.index)` discards `slot.generation`, so a stale
handle written into a uniform block cannot be detected — by either backend. The WebGPU comment above
names this; nothing acts on it.

### Why this is worth doing now and was not before

With one backend the identity was free and the leak was invisible. A second backend makes it a real
cost: every binding-model difference now has to be expressed as a reversal of D3D12's scheme rather
than as its own scheme. The Metal port hits the same wall from the other side (`gpuAddress`
translation at dispatch).

---

## 2. Design

### The seam

`IResourceManager` gains the mapping, as the one place it lives:

```cpp
[[nodiscard]] uint32_t GetBindlessIndex(BufferHandle handle) const noexcept;
[[nodiscard]] uint32_t GetBindlessIndex(TextureHandle handle) const noexcept;
[[nodiscard]] uint32_t GetBindlessIndex(SamplerHandle handle) const noexcept;
```

D3D12 returns the descriptor index it allocated. WebGPU returns the slot index unchanged — which is
what its bind-group assembly wants — and `GetBufferBindingBySlotIndex` is deleted, because the
translation now runs in the direction the data flows.

### The allocator

A `DescriptorAllocator` owns each heap and hands out indices:

```cpp
uint32_t Allocate();          // throws when the heap is full
void     Free(uint32_t idx);  // returns it to the free list
```

`Texture` and `Buffer` already carry a `D3D12_CPU_DESCRIPTOR_HANDLE`; they gain the index that
produced it. Deferred destruction frees the descriptor on the same gate that reclaims the slot — the
descriptor must outlive in-flight work exactly as the resource does, which is what
`RetireDeferred` already sequences.

### What falls out

- **One texture pool.** `m_Textures` and the texture half of `m_CbvSrvUavSlots` merge. Heap
  occupancy is no longer the reason to keep two.
- **`TextureHandle::usage` stops being a discriminator.** It keeps its descriptive meaning; nothing
  indexes on it.
- **`std::variant<Buffer, Texture>` goes away.** `slot_vector<Buffer>` and `slot_vector<Texture>`,
  each sized by how many of that resource can exist.
- **Pool size and heap size decouple.** `maxCbvSrvUavs` currently means both "how many resources"
  and "how many descriptors"; they become separate numbers with separate reasons.

### The hard part, and the option taken

`Uniforms::operator=` is where a handle becomes a number, and it has no ResourceManager — it is
constructed from a pipeline (`Uniforms.h:321-323`) and its `AccessorBase` carries only
`(m_Data, offset, node)`.

Two ways out:

1. **Resolve eagerly.** `Uniforms` holds a `ResourceManagerRef`; `operator=` calls `GetBindlessIndex`
   and writes the result as it does today. One virtual call per assignment. The uniform block still
   contains a backend-specific number.
2. **Resolve at record time.** `operator=` writes the *slot* (index + generation, 8 bytes — the
   `DescriptorHandle` is already a `uint2`), and the backend patches or reads it when the block is
   flushed. This is what the WebGPU backend already does in `CollectHandleBindings`; D3D12 would gain
   an equivalent walk it does not have today.

**Take (1).** It is the smaller change, it keeps D3D12's flush a straight `memcpy`, and it puts the
translation at the point where the resource is named rather than at the point where bytes are sent.
(2) is architecturally tidier and makes generations checkable at bind time, but it charges every
D3D12 draw for a per-flush layout walk to fix a problem that is not about performance. Revisit only
if a third binding model turns up that (1) cannot express.

Consequence to accept: `GetBindlessIndex` runs under the pool mutex, and `operator=` is called from
scene-update code. Reads in the manager are already documented as lockless
(`ResourceManager_d3d12.h:275-278` — fixed-capacity pools, storage never moves), so this lookup
follows the same rule and takes no lock.

---

## 3. Staging

Each step builds and passes on its own; the identity survives until D3 removes it, so nothing is
half-migrated at a commit boundary.

* **D1 — `DescriptorAllocator`, unused.** The class plus its tests: allocate to exhaustion, free and
  reallocate, and that a freed index is not handed out twice before `Free`. No wiring.
  *Gate:* new unit tests in `bgl_tests`; nothing else changes.
* **D2 — `GetBindlessIndex` on both backends, returning what the identity returns today.** D3D12
  returns `handle.slot.index`; WebGPU returns the same. A pure seam: every call site can migrate to
  it before the number behind it changes.
  *Gate:* `bgl_tests` and `bgl_webgpu_tests` unchanged and green.
* **D3 — move the call sites onto the seam.** `Uniforms` gains its `ResourceManagerRef`; the ten
  sites in § 1 stop constructing `DescriptorHandle` from a slot. The `DescriptorHandle(slot_handle)`
  constructor is deleted, which is what makes the migration checkable — anything left behind fails to
  compile.
  *Gate:* golden images bit-identical on D3D12 (the numbers have not changed yet, so any diff is a
  bug); `bgl_webgpu_tests` green.
* **D4 — D3D12 allocates descriptors properly.** `CreateTexture`/`CreateStructBuffer` take an index
  from the allocator instead of using the slot index; destruction frees it on the deferred gate.
  This is the step where the two numbers actually diverge.
  *Gate:* golden images within tolerance; `just run bgl_tests -- --gpu-validation`, because a
  mis-freed descriptor is exactly what GPU-based validation catches and nothing else does.
* **D5 — collapse the pools.** `m_Textures` merges into the texture pool, the variant goes,
  `TextureHandle::usage` stops selecting an index space, and `maxCbvSrvUavs` splits into a resource
  count and a descriptor count.
  *Gate:* as D4, plus a test that an RTV-only texture and an SRV texture can hold the same slot index
  without colliding — the failure the old design prevented structurally and this one must prevent
  deliberately.
* **D6 — WebGPU drops the reversal.** `GetBufferBindingBySlotIndex` is deleted;
  `CollectHandleBindings` resolves through the seam.
  *Gate:* `bgl_webgpu_tests` green.

---

## 4. What this does not do

- **Bindless is not removed.** Shaders still index a heap. This changes where the index comes from,
  not what a shader does with it.
- **Generation checking is not added.** D3 makes it *possible* — the manager sees a full
  `slot_handle` at the point of translation and could validate it — but validating there costs a
  branch per assignment and belongs in its own change with its own argument. Noted so the next reader
  does not assume it came for free.
- **Sampler heap is untouched.** Samplers have their own small heap and their own pool, and no
  second pool grew around them; `GetBindlessIndex(SamplerHandle)` exists for symmetry, returning
  `handle.idx`.
- **No FrameGraph or barrier changes.** Resource lifetime and layout tracking key off handles, not
  descriptor indices.

## 5. Risk

The one that matters is **D4 landing silently wrong**. Once slot and descriptor diverge, an
off-by-one or a use-after-free reads a *valid* descriptor for the wrong resource — which renders
something plausible rather than crashing. That is why D4's gate is GPU validation and not just
goldens: a wrong-but-live descriptor can pass a tolerance-based image compare, and the debug layer
alone does not see it.

The cheap mitigation, worth doing in D4 rather than after: have the debug build write a known
sentinel into every freed descriptor, so a stale read lands on something visibly wrong instead of on
whatever took the slot.
