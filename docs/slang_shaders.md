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

`Atomic<T>` lowers to `InterlockedAdd` on DXIL unchanged, so the D3D12 path is unaffected. The
same holds for a buffer element: the atomic target's element type carries the atomic, which is why
the compute-buffer primitive needs a WGSL variant that exposes atomic access (see the port plan).

## No 16-bit integers in WGSL

Core WGSL has no `uint16_t`/`int16_t`. A 16-bit scalar in shared shader code — or reached through an
imported IDL struct — fails the WGSL compile. IDL fields that are 16-bit are packed into `u32` behind
accessors by the codegen for the WGSL target; do not introduce a bare 16-bit scalar in a shader that
must be portable.

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
  front-end (Tint) — the bindless heap lowering is the known case. `slangc` in this toolchain has no
  Tint linked, so that layer is validated by creating the module on a Dawn device
  (`wgpuDeviceCreateShaderModule`) at runtime, not at build time.

## Formatting

`.slang` files are clang-formatted like the C++ — `just format <files...>`.
