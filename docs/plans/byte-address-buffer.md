# byte-address-buffer — implementation plan

## Context

Three GPU-side references are not what their types say.

`SubmeshInstance.material` is an `Entry<IMaterial>`
([libs/bgl/shaders/src/types/SubmeshInstance.slang](../../libs/bgl/shaders/src/types/SubmeshInstance.slang)),
but it indexes one of two buffers — `pbrMaterials` or `looseMaterials`
([libs/bgl/shaders/src/forward/MaterialData.slang](../../libs/bgl/shaders/src/forward/MaterialData.slang)) —
and only `pso` says which. Nothing at the offset records its own kind, so the one pass that draws
both kinds from a single pipeline resolves it from a flag the geometry stage stamps from the PSO
([libs/bgl/shaders/src/Forward_Transparent.slang](../../libs/bgl/shaders/src/Forward_Transparent.slang)).

A `Mesh` carries two mutually exclusive `Entry` fields, `vatState` and `skinnedState`
([libs/bgl/idl/src/Mesh.slang](../../libs/bgl/idl/src/Mesh.slang)), and the shared geometry stage
picks a tier by null-checking them in a fixed order
([libs/bgl/shaders/src/Forward_AnyMesh.slang](../../libs/bgl/shaders/src/Forward_AnyMesh.slang)).

Vertex bytes are a `StructuredBuffer<uint>` read one word at a time
([libs/bgl/shaders/src/types/ByteBuffer.slang](../../libs/bgl/shaders/src/types/ByteBuffer.slang)),
every attribute reassembled by hand
([libs/bgl/shaders/src/util/Vertex.slang](../../libs/bgl/shaders/src/util/Vertex.slang)),
and the word index is turned into a byte address at the point of use — `vertexData.GetStart() * 4`
in [mesh_stage.slang](../../libs/bgl/shaders/src/forward/mesh_stage.slang) — which wraps a `uint`
past 4 GB without a word said anywhere.

No feature is forcing this. It is the seam being made honest before more is built on it: a raw
(byte-addressed) buffer is the primitive a record of varying layout belongs in, and the engine does
not have one.

## Decisions

- **ADR-1 — Vertex data lives in a bindless `ByteAddressBuffer` read with typed `Load<T>`.**
  The RHI gains a raw buffer view, and the shader-side `ByteBuffer` emulation goes. The *float*
  formats become one typed load each; the normalized and integer ones keep their unpacking, because
  a raw buffer does no format conversion — that is what a typed view is for.
  *Rejected: keeping `StructuredBuffer<uint>` with word loads and bit shifts — it is what you write
  when the backend has no raw view, and it hides the byte arithmetic in the caller.*

- **ADR-2 — Materials, and the mesh playback state, each live in one raw arena where every record
  starts with a fixed header.** The header carries the record's type tag; a reference names the
  header. A payload must be handle-free to be raw-loaded at all, which is ADR-8. *Rejected: the tag
  in the reference's low bits (Unity DOTS' shape) — the arena shrinks by the tag width, and every
  reader has to mask before it can load. Rejected: no tag, the type known only from the PSO — that
  is today, and it leaves `Entry<IMaterial>` a lie.*

- **ADR-3 — Static per-PSO specialisation stays; only a pass shared across kinds reads the
  header.** Opaque, alpha-test and hashed PSOs are compiled for one material kind and read the payload
  at a fixed offset without looking at the tag. This is Unreal's material shape and we do not depart
  from it. The shared pass pays for the tag: today its branch is on a `nointerpolation` flag the
  geometry stage stamped from the PSO, and it becomes a 4-byte load per pixel, uniform across the
  primitive and in the same cache line as the payload the pixel loads next. *Rejected: dispatching on
  the tag in every pixel module — one PSO family fewer, a divergent switch in every pixel, and a named
  deviation from every shipping engine.*

- **ADR-4 — One raw arena is at most 2³² bytes, and growth past that is refused, not wrapped.**
  The bound is inclusive: a `uint` byte offset addresses 0 … 2³²−1, so a buffer of exactly 2³² bytes
  is entirely reachable and the arena's check is `> c_MaxRawBufferBytes`. The ceiling is the view's
  rather than ours; the mirror buffer throws before it allocates, the way a `DispatchMesh` past
  65535 meshlets is rejected today. *Rejected: paging an arena across buffers with a `(buffer, offset)`
  pair — every reference doubles for a size no asset here approaches. Rejected: leaving the silent
  overflow — it is the one thing the request asked about.*

- **ADR-5 — D3D12 and Metal get the raw view in the same task.** *Rejected: Metal first — the
  merged feature branch would compile shaders asking for a view one backend cannot make.*

- **ADR-6 — A reference into a raw arena is a byte offset, `0` is null, and the arena's head is a
  reserved, zeroed null record big enough for any payload the arena holds.** Same sentinel rule as
  `Entry`/`Range` and `c_UnboundDescriptorIndex`: a zeroed struct is null, and a null dereference —
  header *and* payload — lands in zeros rather than in the first live record or off the end. The
  owner declares the size when it creates the arena — 16 plus the largest of its payload types, or
  16 alone for an arena of untyped ranges: a null `vertexData` only ever comes from a zeroed
  `Submesh`, whose meshlet count is 0, so nothing reads through it, and task 3 asserts that
  invariant in a debug build. A null record deref is reported too, as `EntryBuffer::Get` does. *Rejected: a
  16-byte block index — reaches 64 GB on paper, but the view cannot, and `Load` wants bytes.
  Rejected: reserving only the header — a null payload read would land in the first real record.*

- **ADR-7 — A record is a `{uint type}` header at a 16-byte-aligned offset with its payload 16
  bytes after; an untyped range is 16-byte-aligned bytes with no header.** `RawEntry<T>` names a
  record, `RawRange` names a range, and the arena allocates both. The tag is a `uint` lane in the IDL
  struct because one header serves every arena and the enums differ per arena (`MaterialType` is one
  byte on the CPU; the header wants a 4-byte lane on every target); the enum lives at the edges — the
  CPU arena is templated on its tag enum so no caller writes a bare integer, and a shader reads the
  tag through the arena's enum at the one branch that looks at it. There is no size in the header:
  a record's size is a function of its type, so the tag is what a debug build checks a load
  against -- in the task that has a caller naming both, since a generic load cannot derive the tag
  its payload type belongs to. The alignment the decoder and every payload load actually
  rely on is 4 bytes — Slang lowers a raw load to what the target can do at that alignment, and task
  1 proves it on the machine's backend before anything rests on it. Records are on a 16-byte grid
  anyway so that a payload sits at its own natural alignment (a `float4` at 16) and a whole-struct
  `Load<T>` can never be emitted wider than the address allows. *Rejected: a 4-byte tag with the
  payload at +4 — every vector field then sits off its natural alignment. Rejected: a `byteSize` in
  the header — redundant with the tag for every reader that knows the enum, and nothing walks an
  arena. Rejected: a header on a vertex range — there is no kind to record and the non-goals forbid
  inventing one.*

- **ADR-8 — A raw arena holding resource handles is bound twice: raw for the payload, typed for
  the handles.** Task 1 established that a bindless resource handle cannot be read out of a raw
  buffer on Metal at all — the element type of a bindless buffer is fixed at its declaration, and a
  raw one declares bytes. The handle's *bytes* still live inside the record; what materialises them
  is a second, typed view of the same allocation, read at `(recordOffset + fieldOffset) / 8`. So a
  payload declares its handle fields as `RawTextureHandle` — the same eight bytes with no resource
  type — and the record stays raw-loadable.

  Proven before it was built: one buffer bound to a `RawBuffer` and an `EntryBuffer<TextureHandle>`
  uniform at the same time raw-loads the record and samples the texture correctly, clean under Metal
  GPU validation. Metal needs no *descriptor* for the second view — a buffer there is an address and
  the shader's declared type is the view — where D3D12's descriptors are typed and it needs a real
  one. `CreateBufferSrv` is the seam, and it hands back a handle with a lifetime on both.

  *Rejected: an `Entry<TextureHandle>` into a separate table. It works on both backends and was
  built (#526), but it puts every handle behind a serial dependency — record, then index, then
  handle — where a second view computes the handle's address from the record's own offset and issues
  both loads in parallel; and it costs a second allocation with a lifetime of its own. Rejected: a
  per-backend macro giving D3D12 the inline handle it can already raw-load — one cache-resident load
  saved on one backend, paid for with a permanent fork in the hottest shader path, which no CI run
  exercises on either backend.*

- **ADR-9 — The two views are one member, and the arena owns both.** Added after task 5. ADR-8 left
  them as two independent uniforms bound from two places: the raw one from the frame graph, the
  typed one from the draw. Nothing then states that they name the same allocation, and a growth
  replaces the resource without announcing it — so correctness rests on `Scene` re-issuing the view
  at one particular instant. That is not a hypothetical: taking the two at different instants is
  the bug that blocked task 5's precheck, and the fix was a call moved between two functions.

  So the arena owns its view and re-issues it *inside* its own growth, where the buffer and the
  descriptor change together and nothing can observe them apart. The instant stops being a thing a
  reader has to know.

  The view is dead weight on D3D12, where a raw-loaded handle *can* become a texture through
  `ResourceDescriptorHeap` — it is Metal that cannot. Carrying it on both backends anyway is
  ADR-8's rejected macro restated one layer up, and it is rejected here for the same reason: the
  saving is a re-read of bytes the payload load already pulled into cache, and the price is a
  permanent per-backend fork in the per-pixel path, on a project where each backend is exercised by
  exactly one environment.

  *Rejected: leaving the two members independent and defending the instant with a test. That is
  what task 5 shipped, and `MaterialArenaGrowth_test` does hold it — but it pins one call site in
  one subsystem, and the next arena that grows a handle field has to rediscover the rule. A hazard a
  comment has to explain is worse than one the types make unreachable. Rejected: teaching the frame
  graph about the typed view, so both come from `resources.GetBuffer`. The graph tracks resource
  state and a view is not a resource; it would be given a second concept to carry for one caller.*

## Non-goals

- Every other buffer stays structured: meshlets, submeshes, vertexMap, indices, bones, samples,
  palettes, clips, geoms, the compute buffers. `Range` / `RangeWithCount` / `Entry` stay for them.
- No runtime material dispatch in the opaque, alpha-test or hashed PSOs; the `PsoType` table keeps
  its `_PBR` / `_LoosePbr` rows.
- No arena larger than 4 GB, and no address wider than 32 bits.
- No third material model, no new payload kind. The header takes one; nothing is added to prove it.
- No change to what a material *is* on the CPU: `IScene`'s per-kind create/update stay, and
  `MaterialHandle` keeps its `materialType` and `layerType`.
- Culling, sorting, the pose pass's blend and the outline pass keep their algorithms; they change only
  where they read a reference that changed shape.

## Acceptance

- Every committed golden in `assets/golden/` matches at its current tolerance — the geometry
  scenes, the four `alpha_test_*`, `pbr_ibl`, `vat_frozen_frames`, `vat_normal_map` — and the A/B
  captures that stand in for goldens hold: `Transparent_test.cpp`'s loose-vs-baked pair,
  `SkinnedRender_test.cpp`'s static-vs-skinned pair, `MotionVectors_test.cpp`, `HashedAlpha_test.cpp`.
- `just run bgl_tests -- --gpu-validation` is clean on the machine's backend after every task.
- A unit test proves the arena refuses growth past 2³² bytes without allocating it.
- A device test proves a record written from C++ by `memcpy` is read back field-for-field by
  `Load<T>` on the machine's backend, that a `float3` and a `float4` each load correctly from a
  4-byte-aligned address that is not 16-byte-aligned, and that a compute shader can store into a
  raw buffer through a bindless writable view.

## What the survey found

**The RHI has no raw view and only D3D12 needs one.** `IResourceManager` creates buffers through
`CreateStructBuffer` and `CreateComputeBuffer` only
([libs/bgl/src/resource/ResourceManager.h](../../libs/bgl/src/resource/ResourceManager.h)); the
D3D12 backend makes one SRV or UAV per buffer, hard-coded `DXGI_FORMAT_UNKNOWN` + `StructureByteStride`
+ `FLAG_NONE` ([ResourceManager_d3d12.cpp](../../libs/bgl/src/d3d12/resource/ResourceManager_d3d12.cpp)
lines 111–142). `D3D12_BUFFER_SRV_FLAG_RAW` appears nowhere. On Metal a buffer is
`device->newBuffer(byteSize)` and nothing else — no view, no stride survives creation
([Buffer_metal.h](../../libs/bgl/src/metal/resource/Buffer_metal.h)); `bindlessIndex` is the pool
slot, resolved to a GPU address per draw
([CommandList_metal.cpp](../../libs/bgl/src/metal/cmd/CommandList_metal.cpp) `MapUniformHandlesToGpuAddresses`),
and the reflection already maps `SLANG_BYTE_ADDRESS_BUFFER` to `HandleKind::kBuffer`
([SlangReflection.cpp](../../libs/bgl/src/uniforms/SlangReflection.cpp) lines 157–162). The lowered
`BufferDesc` is `{byteSize, isUav, debugName}` ([Buffer.h](../../libs/bgl/src/resource/Buffer.h)).

**Slang 2026.7.1 has what the shader side needs.** The core module declares `ByteAddressBuffer` with
`metal` in its capability list, lists it among the types given a bindless `.Handle`, and gives it
`Load<T>(uint)` with unspecified alignment and `LoadAligned<T>` for scalar/vector/matrix `T`.
`GetDimensions` on it is *not* Metal-capable, so the wrapper must not expose one. No pass here
writes a raw buffer, but `ROADMAP.md`'s GPU skinning to a transient vertex buffer will, and a buffer
gets exactly one view — so once vertex bytes are raw, a compute-written vertex buffer needs the
writable raw view to exist on both backends.

**A resource handle cannot be read out of a raw buffer on Metal**, which task 1 established and
ADR-8 is the answer to. The type has to be on the *binding*: a `ByteAddressBuffer.Handle` lowers to
`uint32_t device*`, and nothing recovers a struct type from that pointer, where
`StructuredBuffer<T, ScalarDataLayout>.Handle` lowers to `T device*` and is why materials render
today. `Load<PbrMaterial>` fails at the Metal compile with *"as_type cast from 'unsigned long' to
'texture2d<float, access::sample>' is not allowed"*, and `__getEquivalentStructuredBuffer<T>` — the
core module's own escape — fails with *"member reference base type 'device uint32_t' is not a
structure or union"*, because it re-types nothing. Both were run, not reasoned about. Handle-free
records load correctly, as does a bindless `RWByteAddressBuffer` store.

The runtime compiles per PSO to `SLANG_DXIL` / `sm_6_6` on D3D12 and to `SLANG_METAL` (MSL
source) on Metal ([Device_metal.cpp](../../libs/bgl/src/metal/device/Device_metal.cpp) line 73); the
build's `compile_shader` validates against DXIL only, so a Metal-only break is caught by
running, never by building.

**A wrapper reaches the shader by member name.** `Uniforms::operator=(BufferHandle)` on an 8-byte
struct searches its members against `c_SmartBufferUniformIndices` — `entryBuffer`, `packedBuffer`,
`rangeBuffer` ([constants.h](../../libs/bgl/src/constants/constants.h) lines 39–41,
[Uniforms.h](../../libs/bgl/src/uniforms/Uniforms.h) lines 192–222). `ByteBuffer.slang` names its
member `rangeBuffer` for exactly that reason. A `RawBuffer` wrapper needs its name in that array.

**The mirror buffers.** `GrowableGpuBuffer::Init(stride, capacity, isUav)` is the one GPU storage
under `RangeBuffer`, `EntryBuffer` and `PackedBuffer`; growth is a new resource and descriptor, a
forward copy on the next `FlushGrowth`, and `NextGpuBufferCapacity` doubling to 64 MiB then tapering,
clamped to `uint32_t` *elements*
([GrowableGpuBuffer.cpp](../../libs/bgl/src/scene/GrowableGpuBuffer.cpp)). `RangeBuffer<T>` reserves
element 0, allocates through `TryAllocateSlots` → `Grow` → retry, and does its dirty-block byte
arithmetic in `uint32_t` ([RangeBuffer.h](../../libs/bgl/src/scene/RangeBuffer.h) lines 406–449).
Nothing rejects a byte size: `CreateStructBuffer` computes bytes in `uint64_t` and asserts only that
stride and count are non-zero. The vertex arena is `RangeBuffer<uint32_t> m_VertexDataBuffer`
([Scene.h](../../libs/bgl/src/scene/Scene.h) line 446), fed words by `PackVertices` and
`CookStaticMesh` ([Scene.cpp](../../libs/bgl/src/scene/Scene.cpp) lines 66–76, 1167–1171), and
`SceneDesc::initialVertexBufferByteSize` is already a byte budget rounded up to words.

**Every attribute is 4-byte aligned by construction, and nothing checks it.** All seven
`VertexFormat` sizes are multiples of 4 ([vertex_layout.cpp](../../libs/assetlib/src/vertex_layout.cpp)),
the glTF importer lays them out sequentially from 0, the procedural layout is 0/12/24/32 stride 48.
`LoadWordAtByte` does `>> 2`, so a misaligned offset would be silently truncated today.

**Material storage and resolution.** `Scene` holds `EntryBuffer<idl::PbrMaterial> m_Pbr` and
`EntryBuffer<idl::LoosePbrMaterial> m_Loose` ([Scene.h](../../libs/bgl/src/scene/Scene.h) lines
449–450), created per kind (`CreatePbrMaterial`, `CreateLoosePbrMaterial`), updated per kind, deleted
through one `switch (materialType)` ([Scene.cpp](../../libs/bgl/src/scene/Scene.cpp) lines
1482–1517). `MaterialHandle` is `{materialType, layerType, slot_handle}`
([MaterialHandle.h](../../libs/bgl/include/bgl/MaterialHandle.h)). `SceneView::ResolveShading` writes
`instance.material = handle` and `instance.pso = SubmeshPso(geomType, handle)`
([SceneView.cpp](../../libs/bgl/src/scene/SceneView.cpp) lines 676–695); `PsoType` encodes the
material kind *and* the tier ([PsoType.slang](../../libs/bgl/idl/src/PsoType.slang)), so every
specialised draw already knows what it reads. `ForwardPass` binds the two buffers by a two-row table
([ForwardPass.cpp](../../libs/bgl/src/passes/ForwardPass.cpp) lines 37–50, 384–387) and draws the
whole depth-sorted transparent list with the `kTransparent_StaticMesh_PBR` kernel (lines 472–473),
whose pixel shader branches on `materialIsLoose`, a varying stamped only by
[static_vertex.slang](../../libs/bgl/shaders/src/forward/static_vertex.slang) line 31 from
`IsLoosePso`; VAT and skinned hard-wire it to `0`.

**Playback state.** `m_VatStates` / `m_SkinnedStates` are `EntryBuffer`s on the *view*
([SceneView.h](../../libs/bgl/src/scene/SceneView.h) lines 359–360); `WritePlacement` assigns one of
`mesh.vatState` / `mesh.skinnedState` by `geomType` (SceneView.cpp lines 338–348), `MeshMeta` records
which buffer holds it, and deletion mirrors the branch. The pose pass binds the skinned state buffer
on its own ([SkinnedPosePass.cpp](../../libs/bgl/src/passes/SkinnedPosePass.cpp) lines 39, 79;
[PoseSkinned.slang](../../libs/bgl/shaders/src/PoseSkinned.slang) lines 23, 172). The shader readers
are one line each: [vat_vertex.slang](../../libs/bgl/shaders/src/forward/vat_vertex.slang) line 105
and [skinned_vertex.slang](../../libs/bgl/shaders/src/forward/skinned_vertex.slang) line 69.

**The IDL generator.** Generics (`Entry<T>`, `Range<T>`, `RangeWithCount<T>`) get a Slang copy only;
their C++ mirrors are hand-written and type-erased ([libs/bgl/src/idl/Entry.h](../../libs/bgl/src/idl/Entry.h)
and siblings; [idl/src/CMakelists.txt](../../libs/bgl/idl/src/CMakelists.txt) lines 17–21). Struct
layout is reflected on the host target, or under `StructuredBuffer<T, ScalarDataLayout>` on a DXIL
session when the struct carries a handle ([idlgen.cpp](../../libs/bgl/idl/idlgen.cpp) lines 404–418),
with `--metal-layout` hand-computing MSL rules. Nested IDL structs by value are supported. Enums are
parsed textually and emitted first. Whether Slang's natural layout for a raw `Load<T>` agrees with
the `ScalarDataLayout` the mirror is asserted against is the same open question the device test
closes; the handle-bearing material structs are the case that matters.

**Tests.** Goldens are `assets/golden/<name>.exp.png`, compared by mean squared error
([GoldenImage.cpp](../../libs/bgl/tests/src/util/GoldenImage.cpp)); the scenes and their owning tests
are listed under Acceptance. Skinned and transparent have no committed golden and compare two of
their own captures. `Range_test.cpp` / `Entry_test.cpp` show how a mirror buffer is tested on a real
device with `blockSize = sizeof(T)`; `GrowableCapacity_test.cpp` tests the growth curve with no
device at all, which is the shape the cap test takes. `--gpu-validation` is a Catch2 option each test
copies into `GraphicsOptions`.

**Drift found on the way** — corrected by the task that touches the doc:
[docs/geometry_layout.md](../geometry_layout.md) line 7 says the C++ mirrors are under
`libs/bgl/src/idl/` (they are generated into `<build>/generated/idl/`; only the three hand-written
primitives live there), and line 148 lists a `Vertex` struct at `util/Vertex.slang` that does not
exist (the decoded form is `DecodedVertex` in `vertexdecode.slang`, which also carries joints and
weights). The same stale "emitted into src/idl" claim sits in
[libs/bgl/idl/src/CMakelists.txt](../../libs/bgl/idl/src/CMakelists.txt) line 23 and
[scripts/gen_idl.py](../../scripts/gen_idl.py) line 8, both of which task 2 opens.

## What changes

- **RHI** (`libs/bgl/src/resource`, `d3d12/resource`, `metal/resource`): a `CreateRawBuffer`
  beside `CreateStructBuffer`, taking a byte size; D3D12 creates the SRV/UAV as `R32_TYPELESS` +
  `RAW`, Metal as today. `BufferDesc` says which view it holds. Could break: a barrier or copy that
  assumed every buffer has a stride — none found, but `GetBufferDesc` callers are the place to look.
- **Uniforms** (`libs/bgl/src/uniforms`, `constants.h`): `rawBuffer` joins the smart-buffer member
  names. Could break: nothing — the name search only widens.
- **Shader primitives** (`libs/bgl/shaders/src/types`, `libs/bgl/idl/src`): `RawBuffer` wrapping
  `ByteAddressBuffer.Handle`, and `RawComputeBuffer`, the `RWByteAddressBuffer.Handle` typealias
  beside `ComputeBuffer`; `RawEntry<T>` (generic, so a Slang copy and a hand-written mirror like
  `Entry`), `RawRange` and `RecordHeader` (concrete, so generated into C++ like every other IDL
  struct); `ByteBuffer` deleted.
- **Mirror buffer** (`libs/bgl/src/scene`): `RawBuffer<Tag>`, named for the Slang wrapper it is read
  through as `RangeBuffer` is — the RHI's descriptor is `RawViewDesc`, for the view rather than the
  buffer, so this name stays where the symmetry wants it. Composed over a
  `RangeBuffer<RawBlock>` whose element is a 16-byte block — so allocation, the reserved element 0,
  dirty tracking and growth are `RangeBuffer`'s, not a fourth allocator's. What it adds is the unit
  conversion (a block index in, a byte offset out), the reserved null record, `AddRecord(Tag, bytes)`
  which writes the tag ahead of the payload, `AddBytes` for a headerless range, and the refusal past
  2³² bytes. `RangeBuffer`'s dirty-block and copy arithmetic is widened to 64 bits, since a 16-byte
  element at the cap wraps its `uint32_t` byte offsets. `GrowableGpuBuffer` learns the raw view, which
  `RangeBufferDesc` asks for.
- **Vertex data** (`Scene`, `Submesh`, `mesh_stage`, `vertexdecode`, `util/Vertex`): the arena is a
  `RawBuffer`, `Submesh.vertexData` a `RawRange`, the decoder loads `float2/3/4` and `uint` directly.
  Could break: every golden, the overflow and delete tests — which is why they are the gate.
- **Materials** (`Scene`, `SceneView`, `MaterialHandle`, `ForwardPass`, `MaterialData.slang`,
  `Forward_Transparent`, `common.slang`, the three vertex modules): one arena bound twice per
  ADR-8 — raw for the payload, typed for the handle fields, which the payload declares as
  `RawTextureHandle` — headers typed by
  `MaterialType`; `SubmeshInstance.material` a `RawEntry<IMaterial>`; the transparent pixel
  shader reads the header and the `materialIsLoose` varying and `IsLoosePso` go. `MaterialHandle`'s
  handle becomes the arena's, so the slot-index bargain `IScene::DeleteMaterial` and
  `docs/bgl_api.md` state is restated for byte offsets — and it is a worse bargain, since a stale
  offset into a variable-size arena can land mid-record rather than on the next tenant; the doc says
  so rather than pretending otherwise, and `gamelib`'s reference holding stays what makes it safe.
  Could break: the epoch re-resolve's "write back only when the offset changed" test, the
  delete-then-reuse tests, the override tests.
- **Playback** (`Mesh`, `SceneView`, `Forward_AnyMesh`, `vat_vertex`, `skinned_vertex`,
  `SkinnedPosePass`, `PoseSkinned`): one `RawEntry<IPlayback> playback` on `Mesh` in place of two
  fields, one arena on the view, the tier read from the header. Could break: the pose pass, which
  reads skinned state by entry from a list it builds itself.
- **Docs**: `docs/rhi.md` (the raw view), `docs/uniforms.md` (the member name), `docs/slang_shaders.md`
  (the wrapper and its Metal limits), `docs/idlgen.md` (the hand-written mirrors it enumerates gain
  `RawEntry`), `docs/geometry_layout.md` (the byte arena, `RawEntry`, `RecordHeader`, and the drift
  above), `docs/bgl_api.md` and the `IScene::DeleteMaterial` contract (the offset bargain),
  `docs/asset_standards.md` (its "adding a shading model" recipe names the per-kind `EntryBuffer`,
  the `c_MaterialBuffers` row and the `materialIsLoose` boolean, all of which go),
  `docs/passes.md` (the `Forward_AnyMesh` tier rule and the pose pass's input list),
  `docs/skinning.md` / `docs/vat.md` (where the state lives).

## The tasks in order

1. **`feat(bgl): a raw buffer view on both backends`** — `CreateRawBuffer`, the D3D12 raw SRV/UAV,
   the Metal pass-through, `rawBuffer` in the uniform names, `types/RawBuffer.slang` with `Load<T>`.
   Unused by any pass; the tests are its only caller, and the PR says so. *Gate:* a new `[raw]` device
   test, GPU validation clean, with three cases on the machine's backend. The *record* case:
   `Load<VatState>` and `Load<CullView>` over bytes `memcpy`'d from their C++ mirrors — the matrix
   and the fixed array are where a target's packing would diverge from the `ScalarDataLayout` the
   mirror asserts. The *vertex* case: `Load<float3>` and `Load<float4>` from a base that is
   4-aligned and not 16-aligned, which is what a 20-byte-offset tangent in a stride the importer
   can produce looks like. The *writable* case: a compute shader stores through a
   `RawComputeBuffer`, proving the raw UAV on D3D12 and that `RWByteAddressBuffer.Handle` lowers on
   Metal. All three run before anything is built on the answers — which is how ADR-8 was found.

2. **`feat(bgl): a byte arena the scene can allocate records in`** — `scene/RawBuffer.h` over
   `RangeBuffer<RawBlock>`, `RawEntry<T>` in the IDL with its hand-written mirror, `RawRange` and
   `RecordHeader` generated, the reserved null record sized by the owner, the tag write, the growth
   refusal, `RangeBuffer`'s byte arithmetic widened to 64 bits, and the three stale "emitted into
   src/idl" lines corrected. Still unused by a pass. *Gate:* `Raw_test.cpp` in `Range_test.cpp`'s
   shape — the reservation and its size, every offset a multiple of 16, the tag at each record, no
   header on a range, dirty blocks, growth preserving offsets; and the device-free cap test in
   `GrowableCapacity_test.cpp`'s shape — the growth arithmetic throws past 2³² bytes, and a dirty
   block at the top of the address space computes its byte range without wrapping.

3. **`feat(bgl): vertex data as a raw arena`** — ADR-1 landed: `Scene`'s vertex arena, `Submesh`'s
   `vertexData` a `RawRange` (no header, per ADR-7), the decoder loading typed values at 4-byte
   alignment, a debug assert that a submesh with meshlets has a non-null `vertexData` (ADR-6's
   invariant), `ByteBuffer.slang` deleted, `geometry_layout.md` corrected. *Gate:* every golden and
   A/B capture under Acceptance, `SceneOverflow_test`, `MeshDelete_test`, `--gpu-validation`.

4. **`feat(bgl): a second, typed view of a buffer`** — ADR-8's mechanism, with no user yet:
   `CreateBufferSrv` beside `CreateSrv`, a `BufferSrvHandle`, the D3D12 structured descriptor, the
   Metal side — which resolves to the buffer's own bindless index, a buffer there being an address,
   but still takes a view slot so the lifetime contract is the same on both — and
   `RawTextureHandle` in the IDL. *Gate:* a `[twoview]` device test — one allocation bound as a
   `RawBuffer` and an `EntryBuffer<TextureHandle>` at once, raw-loading a record and sampling the
   texture whose bytes sit inside that record — clean under GPU validation. The D3D12 descriptor is
   the half this machine cannot run, and the PR says so.

5. **`feat(bgl): materials in one raw arena behind a header`** — ADR-2, ADR-3, ADR-7 and ADR-8 for
   materials: the arena on `Scene` bound raw and typed, payload handle fields as
   `RawTextureHandle`, `MaterialHandle`, `RawEntry<IMaterial>`, the transparent pass reading the
   tag, the varying removed, the offset bargain restated in `IScene.h` and `docs/bgl_api.md`, the
   shading-model recipe in `docs/asset_standards.md` rewritten for one arena. *Gate:* every geometry
   golden (they all draw PBR), `alpha_test_*`, `pbr_ibl`, `HashedAlpha_test`, the transparent
   loose-vs-baked pair, `PsoSelection_test`, `TemporalEpoch_test`, `MaterialTextureDelete_test`,
   `MaterialOverrideRender_test`, `MeshDelete_test`, `--gpu-validation`.

   The arena grows, and a growth replaces the resource its typed view describes, so the arena
   re-creates the view whenever the buffer handle it last viewed changes — nothing announces a
   growth, and `CreateBufferSrv`'s `@post` says so. The refresh has to happen in `ImportResources`,
   beside the import: the graph takes the buffer and the draw takes the view, and a frame that takes
   them at two instants pairs a grown buffer with a released view. `MaterialArenaGrowth_test` is
   what holds that instant — a draw on either side of a growth, which nothing else in the suite does.

   One correction to the header paragraph above: the tag is checked on *both* sides, but not the
   same way. The shader's `LoadRecordAs` keeps a `dbg_assert`, since a mismatch there is bgl's own
   bug. The CPU-side check is a caller passing a handle whose type disagrees with the record it
   names, which `IScene.h` already documents as throwing — so it throws in every build, like the
   offset check beside it.

6. **`feat(bgl): the mesh playback tier behind a header`** — the same for `Mesh.playback`: the arena
   on the view, `Forward_AnyMesh` dispatching on the tag, the pose pass reading through it, the
   tier rule and the pose pass's inputs in `docs/passes.md` rewritten. *Gate:*
   `vat_frozen_frames`, `vat_normal_map`, `SkinnedRender_test` in full, `MotionVectors_test`, the
   outline selection case, `--gpu-validation`.

7. **`refactor(bgl): a raw arena owns its own handle view`** — ADR-9. The two views become one
   member: `RawBuffer` gains the typed handle view beside its raw one, the CPU-side `RawBuffer<Tag>`
   owns the `BufferSrvHandle` and re-issues it *inside* its own growth, and `Uniforms` gains the
   overload that writes both descriptors from one assignment. `Scene::RefreshMaterialHandleView`,
   `m_ViewedMaterialBuffer` and the instant-sensitivity in `ImportResources` all delete.
   `ForwardPass` binds the pair from the draw and keeps declaring the buffer to the frame graph for
   barriers alone — `ImportBuffer` only stores the handle, so `resources.GetBuffer` returns the same
   value either way. An arena with no handles (the vertex arena) names a placeholder element type
   and leaves the view unbound, which `c_UnboundDescriptorIndex` already reserves index 0 for.
   *Gate:* `MaterialArenaGrowth_test` unchanged and still passing — it pins the behaviour this task
   makes structural — plus `Uniforms_test` for the new overload, `BindlessIndex_test`, and the
   geometry goldens under `--gpu-validation`.

   Ordered before the docs task on purpose: task 8 writes the arena's design into `docs/`, and the
   hazard this removes is one of the longer things that page would otherwise have to explain.

8. **`docs: the raw arena outlives its plan`** — what the tasks left in this file that describes the
   code as it now is moves into the subsystem pages named above; this file is deleted. The landing PR
   carries the deletion.
