# Slang Shaders

Every shader in `libs/bgl/shaders/src` is one Slang source, and **both backends compile it at
runtime** from the staged Slang — to DXIL on D3D12, to MSL via `newLibraryWithSource` on Metal.

On a D3D12 build only, there is a build-time pass over the same sources: the `compile_shader`
entries in [`libs/bgl/shaders/CMakeLists.txt`](../libs/bgl/shaders/CMakeLists.txt) invoke `slangc`
per entry point, so a construct the target rejects is a build failure rather than a runtime
surprise. The `.dxil` it produces is validation output only, and nothing loads it. A Metal build
does not run this step at all — `libs/bgl/CMakeLists.txt:128-129` adds the `shaders` subdirectory
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

## A raw buffer holds bytes, and never a resource handle

`RawBuffer` ([types/RawBuffer.slang](../libs/bgl/shaders/src/types/RawBuffer.slang)) wraps
`ByteAddressBuffer.Handle`; `RawComputeBuffer` is its writable counterpart. The buffer must have been
created by `CreateRawBuffer` — a view is chosen once, so binding a structured buffer here reads
undefined bytes rather than failing.

What the arena behind it holds is two things, and the accessors follow: a **record**, which a
`RawEntry<T>` names and which starts with a `RecordHeader` naming its kind, its payload
`cRawPayloadOffset` bytes later; and a **range** of bytes with no header, named by a `RawRange`, for
data whose kind whatever names it already records.

| | |
|---|---|
| `Load<T>(byteOffset)` | a value at an absolute offset — what a decoder with its own layout rule uses |
| `LoadRecordAs<P>(entry)` | a record's payload. The payload type is named separately from the entry's, because an entry is typed by the interface a record satisfies and a load must name the concrete payload behind it |
| `LoadTag(entry)` | the kind in a record's header, for the one pass that draws more than one |
| `LoadInRange<T>(range, byteOffset)` | a value inside a headerless range |

Each asserts its reference is non-null in a debug build (`kNullRawDeref`); a null one reads the
arena's reserved head, which is zeros rather than a live record.

`Load<T>` is for a `T` that **declares no resource type**. The element type of a bindless buffer is
fixed at its declaration, and a raw one declares bytes: on Metal a `ByteAddressBuffer.Handle` lowers
to `uint32_t device*`, so Slang reconstructs `T` from loaded scalars and a `Texture2D.Handle` field
becomes `as_type<texture2d<...>>(ulong)`, which MSL rejects — as does the core module's
`__getEquivalentStructuredBuffer<T>`, which re-types nothing.

A record needing a texture keeps the handle's bytes anyway, as a `RawTextureHandle` — the same eight
bytes with no texture in the type — and samples them through a **second, typed view of the same
allocation**. The raw view reads the record; the typed view is what makes a texture of the bytes
inside it.

`RawHandleView<T>` ([types/RawHandleView.slang](../libs/bgl/shaders/src/types/RawHandleView.slang))
is that view, and it is addressed in the arena's own coordinates — `GetAt(byteOffset, index)`, the
stride divide inside the type. Deliberately **not** an `EntryBuffer<T>`: nothing in it is an
allocated element, there is no reserved null slot, and most offsets are not a `T` at all. What makes
one a `T` is the payload layout rule — handles lead a payload and are contiguous — which the
record's own struct owns and `Scene.cpp` pins with `static_assert`s.

`RawHandleArena<T>` pairs it with the raw view, because they are one allocation: bound separately
they can be handed different buffers. The CPU arena owns both and re-issues the view *inside* its
own growth, so there is no instant at which they disagree, and `Uniforms` writes both descriptors
from one assignment. One view reads one element type, so a payload holding two kinds of handle needs
a second view, not a wider one.

## A tag enum a shader returns must be one the target can express

An enum a shader only *compares against* is folded to a literal and never appears in the generated
code. An enum a function **returns** is emitted as a type — and HLSL has no `uint8_t`, so a tag
declared `: uint8_t` compiles here, passes every Metal test, and fails DXC with
`unknown type name 'uint8_t'`. Tag enums are therefore `uint32_t`, as `PsoType` always was.

This is worth knowing because the check that catches it is narrow. The `compile_shader` entries in
[libs/bgl/shaders/CMakeLists.txt](../libs/bgl/shaders/CMakeLists.txt) validate to DXIL at build
time, and there is no `dxcompiler` on macOS at all — so on a Metal machine that validation does not
run. A shader missing from that list is checked by nothing until it reaches a Windows runtime, which
is how `MaterialType : uint8_t` reached master: every forward shader imports `MaterialData`, but only
`Forward_Transparent` calls `LoadMaterialKind`, and it was the one shader not in the list.

`GetDimensions` is deliberately not exposed: the core module marks it HLSL-only, so a wrapper
carrying it would compile on D3D12 and fail on Metal.

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
