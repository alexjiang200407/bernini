# Slang Shaders

Every shader in `libs/bgl/shaders/src` is one Slang source, and **both backends compile it at
runtime** from the staged Slang — to DXIL on D3D12, to MSL via `newLibraryWithSource` on Metal.

On a D3D12 build only, there is a build-time pass over the same sources: the `compile_shader`
entries in [`libs/bgl/shaders/CMakeLists.txt`](../libs/bgl/shaders/CMakeLists.txt) invoke `slangc`
per entry point, so a construct the target rejects is a build failure rather than a runtime
surprise. The `.dxil` it produces is validation output only, and nothing loads it. A Metal build
does not run this step at all — `libs/bgl/CMakeLists.txt:127-128` adds the `shaders` subdirectory
under `RENDERER_BACKEND STREQUAL "DX12"` and nowhere else — so on macOS a bad shader surfaces when
the pass that needs it is first built, not at compile time.

## Atomics: `Atomic<T>`, never a plain field + `InterlockedAdd`

An atomically-updated variable is declared `Atomic<T>` and touched through its methods (`.store`,
`.add`, `.load`, …), and **every** access goes through them, not just the atomic bump.

```hlsl
groupshared Atomic<uint> gCount;
gCount.store(0u);
gCount.add(1u);            // discards the pre-add value, as InterlockedAdd's out-param did
let n = gCount.load();
```

A buffer element works the same way — the element type carries the atomic — through
`AtomicComputeBuffer<T>` (in
[`types/ComputeBuffer.slang`](../libs/bgl/shaders/src/types/ComputeBuffer.slang)), the atomic
counterpart of `ComputeBuffer<T>`. The debug record buffer
([`debug/dbg.slang`](../libs/bgl/shaders/src/debug/dbg.slang)) is the worked example.

When the atomic target is a **field of an IDL struct** (e.g. `DispatchArgs.threadCountX`,
`CullStats.tested`), make that field `Atomic<uint>` in the IDL source. `bgl_idlgen` maps
`Atomic<uint>` to a plain `uint32_t` in the C++ mirror — layout-identical, so `sizeof`/`offsetof` are
unchanged and all CPU code is untouched. A non-atomic writer of the same struct writes the field
through `.store`: a positional aggregate initializer cannot fill an atomic member.

## Buffers are bindless

The `types.*Buffer` primitives wrap Slang's bindless handle — `StructuredBuffer<T>.Handle`,
`RWStructuredBuffer<T>.Handle` — which lowers to an index into the D3D12 descriptor heap. A shader
never carries a `register(tN, spaceM)`: `pipeline_util::BuildPipelineLayout` links a PSO's entry
points into one program, so bytecode and reflection come from the same link and always agree.

`EntryBuffer<T>` additionally asks for `ScalarDataLayout`, giving tight C-compatible packing so its
element matches the CPU mirror `bgl_idlgen` emits — the default structured-buffer layout
16-byte-aligns nested handle structs, which the mirror does not.

## A constant buffer may not mix data and resources below the top level

`Uniforms` sums a member's offset on the way down the reflected tree, so a nested struct that holds
both plain data and resource handles cannot be placed. The reflection asserts rather than binding the
wrong slot. Keep resources at the top level of the constant buffer, as `ExpansionData` does.

## Declare a `ConstantBuffer` in the module that uses it, not in a shared one

A `ConstantBuffer` global is a shader parameter, so declaring one in a widely-imported module adds it
to the layout of **every** program that imports that module -- including the ones whose entry points
never read it. That shifts the bindings of everything else those programs declare, and the failure is
silent: materials sample the wrong descriptor and the frame comes back black rather than erroring.

*Bug precedent:* the temporal-AA work needed the jitter offsets in the pixel stage and moved
`ConstantBuffer<ViewData> viewData` into `forward/common.slang`, which every pixel module imports.
Eight test cases began rendering black. The fix was to keep the declaration where only the geometry
stages see it -- `forward/mesh_stage.slang` -- and hand the pixel stage a value it had already
corrected. When the pixel stage needs something a constant buffer holds, prefer passing it through the
stage output over widening who can see the buffer.

## Formatting

`.slang` files are clang-formatted like the C++ — `just format <files...>`.
