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

- **CPU-built, uploaded structs** (e.g. `idl::VertexLayout`, built field-by-field and written to a
  buffer, never `memcpy`'d from disk): just **widen** the field to `uint`. The GPU struct grows a
  little; nothing external constrains its size. The CPU mirror follows automatically.
- **Structs `memcpy`'d from a fixed cooked format** (e.g. `Meshlet` from a baked mesh): the byte
  width must be preserved, so **pack** two 16-bit values into one `uint` (hi/lo) behind accessors —
  widening would break the on-disk layout.

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
