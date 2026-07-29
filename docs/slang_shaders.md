# Slang Shaders

Every shader in `libs/bgl/shaders/src` is one Slang source that must compile to **two** targets: DXIL
for the D3D12 backend, and WGSL for the WebGPU backend. The build validates both — `compile_shader`
entries in [`libs/bgl/shaders/CMakeLists.txt`](../libs/bgl/shaders/CMakeLists.txt) invoke `slangc`
per target so a construct one target rejects is a build failure, not a runtime surprise.

WGSL is the stricter target and the source of every rule below. DXIL accepts a superset, so "write it
the WGSL way" is the single guideline that keeps a shader portable — and it costs the D3D12 path
nothing, because Slang lowers the portable form back to the same DXIL.

## Atomics: `Atomic<T>`, never a plain field + `InterlockedAdd`

An atomically-updated variable must be declared `Atomic<T>` and touched through its methods
(`.store`, `.add`, `.load`, …). WGSL only permits atomic operations on an `atomic<T>` location;
`InterlockedAdd` on a plain `uint` — groupshared or in a buffer — fails to compile to WGSL.

```hlsl
// portable
groupshared Atomic<uint> gCount;
gCount.store(0u);
gCount.add(1u);            // discards the pre-add value, as InterlockedAdd's out-param did
let n = gCount.load();

// D3D12-only — rejected by the WGSL target
groupshared uint gCount;
InterlockedAdd(gCount, 1u, prev);
```

`Atomic<T>` lowers to `InterlockedAdd` on DXIL unchanged, so the D3D12 path is unaffected. A buffer
element works the same way — the element type carries the atomic — through `AtomicComputeBuffer<T>`
(in [`types/ComputeBuffer.slang`](../libs/bgl/shaders/src/types/ComputeBuffer.slang)), the atomic
counterpart of `ComputeBuffer<T>`:

```hlsl
AtomicComputeBuffer<uint> counts;   // array<atomic<u32>> in WGSL, RWStructuredBuffer<uint> on DXIL
counts[i].add(1u);
let n = counts[i].load();
```

WGSL forbids a plain read of an `atomic<u32>`, so **every** access to an atomic buffer goes through
`.load`/`.store`/`.add`, not just the atomic bump — the debug record buffer
([`debug/dbg.slang`](../libs/bgl/shaders/src/debug/dbg.slang)) is the worked example.

When the atomic target is a **field of an IDL struct** (e.g. `DispatchArgs.threadCountX`,
`CullStats.tested`), make that field `Atomic<uint>` in the IDL source. `bgl_idlgen` maps
`Atomic<uint>` to a plain `uint32_t` in the C++ mirror — layout-identical, so `sizeof`/`offsetof`
are unchanged and all CPU code is untouched. A non-atomic writer of the same struct writes the field
through `.store` (a positional aggregate initializer cannot fill an atomic member).

## No 16-bit integers in WGSL

Core WGSL has no `uint16_t`/`int16_t`. A 16-bit scalar in shared shader code — or reached through an
imported IDL struct a shader loads — fails the WGSL compile. Do not introduce a bare 16-bit scalar in
a portable shader or in an IDL struct one loads. To remove an existing 16-bit field, the fix depends
on whether the struct's byte layout is fixed:

- **CPU-built, uploaded structs** (e.g. `idl::VertexLayout`, or `idl::Meshlet`, which `Scene`
  meshletizes field-by-field at load — nothing in the cooked `.bmesh` carries a `Meshlet`): just
  **widen** the field to `uint`. The GPU struct grows a little; nothing external constrains its
  size. The CPU mirror follows automatically. `Meshlet` and `PsoType` were both widened this way.
- **Structs `memcpy`'d from a fixed cooked format**: the byte width must be preserved, so **pack**
  two 16-bit values into one `uint` (hi/lo) behind accessors — widening would break the on-disk
  layout. No geometry struct is in this class today.

An `enum : uint16_t` is the same defect and easy to miss, because it fails where the enum is *used*
rather than where it is declared: a 16-bit enum passed by value reaches the WGSL backend as an empty
parameter type (`fn IsLoosePso_0( pso_1 : )`), not as a diagnostic naming the enum.

## An out-of-range buffer read does not return zero in WGSL

DXIL gives zeroes for a read past the end of a structured buffer; WGSL's bounds robustness **clamps
to the last element** instead. So the usual padded-dispatch idiom — dispatch a whole number of
groups and let the tail threads read a zeroed, and therefore ignored, element — silently processes a
duplicate of the last element on WebGPU. A histogram written that way over-counts by however many
threads the dispatch was padded with.

Guard the tail on the real count, which `PackedBuffer::GetCount()` reads from the buffer itself
(`GetDimensions`, `arrayLength` in WGSL) so no CPU value has to be plumbed in. Where the guard sits
above a `GroupMemoryBarrierWithGroupSync()`, wrap the body rather than returning early: the barrier
must stay in uniform control flow.

## Struct size is rounded up to alignment in WGSL

A WGSL struct's size is rounded up to its own alignment, which the CPU mirror does not do. So
`idl::Mesh` — a `float4x4` (align 16) plus a `RangeWithCount` — packs to 72 bytes on the CPU but has
an **80-byte** stride on the GPU, and the second element of the buffer would be read from the wrong
offset. `ScalarDataLayout` controls member offsets, not this rounding. Pad such a struct explicitly
in the IDL source so both mirrors agree; `Mesh` carries a `pad` field for exactly this.

## No bindless on WGSL — bind each buffer explicitly

The `types.*Buffer` primitives are `RWStructuredBuffer<T>.Handle` — Slang's **bindless** handle,
which lowers to a D3D12 descriptor heap. For the WGSL target slangc lowers that heap to
`array<array<u32>>` (a nested runtime-sized array), which **core WGSL forbids** and Dawn's front-end
(Tint) rejects: *"an array element type cannot contain a runtime-sized array."* A plainly bound
buffer — a non-`.Handle` `RWStructuredBuffer<T>` — instead emits a real
`@group(g) @binding(b) var<storage> …` that Tint accepts. Both verdicts are pinned by
`TintValidation_test`.

So the WGSL backend cannot use the `.Handle` primitives: it binds each buffer to an explicit
`(group, binding)` slot, taken from Slang reflection, and assembles bind groups on the CPU side
instead of indexing a heap. The D3D12 backend keeps the bindless heap. Buffer primitives therefore
need a per-target form (bindless on DXIL, plainly bound on WGSL).

Textures and samplers follow the same seam, with one split that buffers do not have. `SamplerHandle`
and `idl.TextureCubeHandle` are only ever *constant-buffer-resident*, where Slang hoists the plain
WGSL form to its own binding — so on WGSL they simply become `SamplerState` / `TextureCube`. But
`idl.TextureHandle` lives inside **buffer-resident** structs (the material tables), and a storage
buffer cannot hold a texture on any target: its WGSL form keeps the `uint2` footprint the CPU
writes, with the sample methods absent so a use fails at compile time. Sampling a material texture
on WebGPU is the W4 atlas redesign; `StrideProbe_test` pins the footprint.

A constant buffer's *plain* members go somewhere else again. Slang gathers them into a std140
`var<uniform>` block at the constant buffer's own binding and starts the resource slots after it, so
one `ConstantBuffer<T>` that mixes data with buffers — `ExpansionData` is the case that matters —
becomes a uniform binding plus one storage binding per buffer. The CPU-side `Uniforms` bytes mirror
that split: plain members keep the std140 offsets Slang assigned, so `[0, uniformBlockSize)` uploads
with one memcpy, and resource handles are packed after the block, where only their slot index is
read back. `float4x4` lands as a column-major `array<vec4<f32>, 4>`, which is byte-identical to
`glm::mat4` and to what the D3D12 constant buffer expects — so the same bytes serve both backends,
as `MixedUniform_test` pins. Because those offsets are struct-relative and summed on the way down,
a struct may not mix plain data with resources *below* the top level; the reflection asserts rather
than binding the wrong slot.

## No mesh or amplification shaders in WGSL

WebGPU has neither stage. The D3D12 geometry path is amplification + mesh (`ASMain`/`MSMain`); the
portable path replaces them with a compute expansion kernel plus a vertex-pulling draw. New geometry
logic belongs in that shared, stage-agnostic form — the buffer-walking and vertex-decode code — not
in a mesh-stage entry point that only DXIL can build. See
[the WebGPU port plan](./plans/webgpu_port_plan.md) for the expansion/vertex-pulling design.

## Enforcement

- **slangc**, per target, at build time: the `compile_shader` entries fail the build on any construct
  the target rejects. This catches everything above.
- **Tint**, beyond slangc: some constructs `slangc` emits as WGSL are still rejected by Dawn's WGSL
  front-end (Tint) — the bindless heap above is the confirmed case. `slangc` in this toolchain has no
  Tint linked, so that layer is validated by creating the module on a Dawn device
  (`wgpuDeviceCreateShaderModule`) — `TintValidation_test` is the harness.

## Formatting

`.slang` files are clang-formatted like the C++ — `just format <files...>`.
