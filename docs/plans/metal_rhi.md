# A Metal backend for the bgl RHI

`bgl` has one backend, `bgl_d3d12`, and it runs on hardware nobody on this project owns. This branch
adds `bgl_metal` behind the same `RENDERER_BACKEND` seam, far enough that `bgl_tests` passes, the SDL
examples render in a window, and the Qt editor runs — all on the macOS machine the work is done on.

A Metal backend was on master once (PRs #56–#72) and was removed in 576ccee. **The first task reverts
that removal**, because measuring the drift showed the old code is far closer to the current RHI than
the removal commit's summary suggests — see below.

## Why now

The removal commit gave three reasons. Two have expired and one has inverted.

- *"The iOS target that motivated it is no longer the direction."* Still true, and irrelevant — the
  target here is the desktop this repository is developed on.
- *"The multi-context work is about to change the RHI's per-frame ownership."* It has since landed
  (#82–#101). `IResourceManager` now tracks N submission timelines, the render context owns its queue
  and list, and the frame path was extracted into a backend-free `RenderContext`. A backend written
  today is written against a settled interface.
- *"CI compiled the backend but never exercised it."* This is the inversion. Every change since #175
  has been verified on a Windows CI leg that builds and does not run — `docs/plans/drop_webgpu_plan.md`
  called this "the hole in the verification" and it has not closed. A Metal backend is the first one
  that can be run, debugged and golden-imaged on the machine holding the keyboard.

## What the survey found

### The revert applies, and the drift is 41 errors

Measured, not estimated. `git revert --no-commit 576ccee` onto `feat/metal-rhi` restores all 29 backend
files with **no conflict** — they are pure additions, so nothing can conflict — and conflicts only in
`CMakePresets.json` and `.github/workflows/ci.yml`. With those resolved the preset configures and
`bgl_metal` compiles to 41 errors, in four headers:

| file | errors |
|---|---|
| `ResourceManager_metal.h` | 26 |
| `Device_metal.h` | 4 |
| `CommandQueue_metal.h` | 4 |
| `CommandList_metal.h` | 3 |
| `Graphics_metal.cpp` | 1 stale include (`bgl/RenderContext.h`) |

Five of the nine `.cpp` files compile untouched. Every error is mechanical interface drift, and it is
the same three changes over and over: `Destroy*` and `CleanupExpiredResources` gained the N-timeline
parameters, `RegisterQueue`/`UnregisterQueue`/`CopyBuffer`/`Flush`/`GetTextureDesc`/`CreateRenderTarget`
are new pure virtuals, and `CreateResourceManager` lost its queue parameter.

The removal commit summarised the old backend as stubbing "CreateScene, CreateSceneView and both
screenshot paths". True, and misleading about the RHI layer: `ResourceManager_metal` already covered
buffers, textures, samplers, RTVs, DSVs, clears, readback and validity checks, with 16
`gunimplemented`s left. Retyping that would be redoing work that was written, reviewed and tested.

**So task 1 is the revert plus the drift repair, and the later tasks make the restored code correct
against today's semantics rather than write it from nothing.** Four hunks of the revert are
deliberately *not* taken:

- the `[metal]` tags on three test files and the `RENDERER_BACKEND_METAL` filter block in
  `tests/src/main.cpp`. The removal commit's criticism of them was right, and D14 replaces them.
- repurposing `macos-clang-debug` as the Metal preset. #170 made it the deliberately backend-free
  preset that keeps `core` and `assetlib` honest under a non-MSVC compiler; Metal gets new
  `macos-clang-metal-debug/release` presets beside it.
- the duplicate hidden `macos` preset fragment the revert reintroduces (CMake refuses to read the
  file until one is removed) — #170's version, which carries the `arm64-osx` triplet, is the one to keep.
- the old macOS CI job verbatim. A Metal leg is wanted, written against the current `ci.yml` so it
  keeps #179's toolchain-keyed vcpkg cache rather than reinstating the bug that fix removed.

### The backend surface is much smaller than it was

`Graphics_metal.cpp` stubbed `CreateScene`, `CreateSceneView` and both screenshot paths because those
were backend code in July. They are not any more. `Scene`, `SceneView`, `RenderContext` (746 lines,
including the screenshot encode) and `RenderTargetBase` are all backend-free, and
`libs/bgl/src/d3d12/Graphics_d3d12.cpp` is down to 333 lines of forwarding.

A backend must supply eight things, and nothing else:

| | Interface | D3D12 size |
|---|---|---|
| device + factories | `IDevice` [libs/bgl/src/device/Device.h](../../libs/bgl/src/device/Device.h) | 297 |
| submission | `ICommandQueue`, `ICommandAllocator` [libs/bgl/src/cmd/](../../libs/bgl/src/cmd/) | 313 |
| recording | `ICommandList`, 20 methods [libs/bgl/src/cmd/CommandList.h](../../libs/bgl/src/cmd/CommandList.h) | 938 |
| resources | `IResourceManager`, 37 methods [libs/bgl/src/resource/ResourceManager.h](../../libs/bgl/src/resource/ResourceManager.h) | 1203 + 319 upload |
| pipelines | `IShader`, `IComputePipeline`, `IMeshletPipeline` | 742 |
| render target | `RenderTargetBase` [libs/bgl/src/gfx/RenderTargetBase.h](../../libs/bgl/src/gfx/RenderTargetBase.h) | 457 |
| conversions | formats, states, barriers | 1150 |
| façade | `GraphicsBase` | 333 |

7108 lines on D3D12. Metal should come in under that: no descriptor heaps, no root signatures, and —
see D6 — most of the barrier vocabulary collapses to nothing.

### Nothing above bgl is Windows-bound

The editor is Qt6 and contains no Win32 (`apps/editor/src/Windows` is UI windows, not Win32). The
examples are SDL3, and `examples/util/DemoWindow.cpp:46-56` already resolves a `CAMetalLayer*` on
`__APPLE__` and documents `RenderTargetDesc::wnd` as holding it — that contract survived the removal
and this branch adopts it rather than inventing another. `libs/core` has a `PLATFORM=MACOS` arm and a
`src/posix` directory, unreachable today only because no preset selects a backend on macOS. The root
`CMakeLists.txt` gates gamelib, the editor and the examples on `TARGET bgl`, so all three appear the
moment a Metal `bgl` exists.

The one place that needs work is `apps/editor/src/Windows/RenderTarget/RenderTargetWindow.cpp:33`,
which passes `winId()` — an `NSView*` on macOS, not a layer. See D11.

### Four facts measured on this machine, not assumed

Apple M3 Pro, macOS 26.5, `slangc` from vcpkg's `shader-slang`, and a probe binary built against the
Metal framework. Scripts and outputs are reproduced in the task PRs.

**1. The device is capable enough, and Xcode is not required.** `MTLGPUFamilyMetal3` and
`MTLGPUFamilyApple9` are both supported, `newResidencySetWithDescriptor:` responds, and
`newLibraryWithSource:` compiles MSL at runtime with only the Command Line Tools installed. Runtime
compilation is the only path the engine uses, so the offline `metal` compiler being absent costs
nothing.

**2. Slang's Metal target emits bindless handles as native 64-bit values, including nested inside a
device struct.** Compiling a `StructuredBuffer<Mat>.Handle` whose element holds a `Texture2D.Handle`
and a `SamplerState.Handle`:

```
struct Mat_0 { float4 tint_0; texture2d<float, access::sample> albedo_0; sampler samp_0; };
struct Uniforms_0 { Mat_0 device* materials_0; float4 device* outBuf_0; uint count_0; };
```

A buffer handle becomes a `device*` (a `gpuAddress`), a texture or sampler handle becomes an 8-byte
`MTLResourceID`. This is the whole bindless story and it needs no emulation — but it means the value
the CPU writes into a handle field is a native id, not a slot index. See D3.

**3. Metal reflection cannot be trusted for constant-buffer layout.** The same field that reflects as
a `uint2` with `offset`/`size` on the HLSL target reflects as `kind: "resource"` on the Metal target,
outside the uniform category — which is why `SlangReflection.cpp` hits its
`HANDLE_UNSUPPORTED_TYPE_KIND(Kind::Resource)` and `gfatal`s. Worse, a cbuffer holding only handles
reports its ordinary-data size as **0** while the emitted MSL lays out real 8-byte pointers, so
trusting `getSize()` yields an empty mirror and a segfault on the first write. Offsets must be
recomputed from field types. (Both were found on the old port; both still reproduce.)

**4. Metal wants exactly the struct layout WGSL wanted, byte for byte.** MSL applies both rules the
C/C++ mirror does not: resource handles force 8-byte alignment, and a struct's size rounds up to its
alignment. Measured by `sizeof` inside a dispatched kernel:

| struct | D3D12 today | Metal needs | WGSL needed (#180's table) |
|---|---|---|---|
| `ChannelSource` | 12 | **16** | 16 |
| `Mesh` | 72 | **80** | 80 |
| `PbrMaterial` | 52 | **64** | 64 |
| `LoosePbrMaterial` | 136 | **176** | 176 |

Every number in the "Metal needs" column is the "now" column of the table in
`docs/plans/drop_webgpu_plan.md` — the layout master carried until 3b4ecc1 removed it eleven days ago.
See D4.

## Design decisions

**D1. metal-cpp, with Objective-C++ only where AppKit is unavoidable.**
Apple's header-only C++ binding keeps the backend in `.cpp` files matching the rest of the tree, and
the old port already proved it builds here. Pinned via `FetchContent` on `bkaradzic/metal-cpp` because
it is not on vcpkg. *Rejected:* writing the backend in `.mm` throughout — it would make every
`bgl_metal` file a different language from every `bgl_d3d12` file for no gain, since metal-cpp covers
`MTL::` and `CA::MetalLayer` completely. The exception is layer-backing an `NSView`, which is AppKit;
that is one small `.mm` in the editor (D11), not in `bgl_metal`.

**D2. The handle *is* the native id; there is no emulated descriptor heap.**
Finding 2 says Slang hands us `gpuAddress` and `MTLResourceID` directly. `bgl`'s own model — a handle
is an index into a manager-owned pool, and what the shader reads is a `DescriptorHandle` — survives
intact; only the *value* differs per backend, which `docs/rhi.md` already anticipates in as many
words ("on D3D12 that handle *is* the descriptor-heap index, a second backend is free to make it a
native resource id"). *Rejected:* binding one large argument buffer as a heap and keeping slot
indices in the handle. It would preserve the D3D12 value exactly, but Slang owns the emission of a
`.Handle` dereference and emits a pointer load, not an indexed one — so this buys index-stability at
the price of forking every shader that touches a handle.

**D3. A resolve seam on `IResourceManager`, and an invariant that native ids are never cached in GPU
memory.**
`DescriptorHandle` is constructed from a slot index at six sites, all of which already hold a resource
manager: `Scene.cpp:880,939,944` and `GetDescriptorHandle()` in `EntryBuffer.h`, `PackedBuffer.h`,
`RangeBuffer.h`. Each becomes `resourceManager->ResolveDescriptor(handle)`, virtual, returning the
slot index on D3D12 and the native id on Metal.

The hazard this creates is staleness: a slot index is stable across copy-on-grow buffer recreation
and a `gpuAddress` is not. It is bounded, and the boundary is worth stating as a rule rather than
discovering later — **a resolved descriptor may be written into a uniform mirror, which is re-stamped
on every bind, but never into a struct buffer that outlives a frame.** Today only texture handles are
written into GPU-resident structs (`Scene.cpp`, into the material buffer) and textures are never
recreated in place, so the rule holds; the task that lands the seam adds an assertion that pins it.

**D4. Reinstate idlgen's padding session, retargeted at Metal.**
Finding 4 means the four generated structs must grow again. 3b4ecc1 removed the third Slang session
and `InjectWgslPadding` cleanly and recently, so the mechanism is recoverable with
`SLANG_WGSL` → `SLANG_METAL` and a rename — the machinery was never WGSL-specific, only its target
was. *Rejected:* hand-writing `pad` members into the `.slang` IDL sources. It needs no third session,
but it makes every future IDL struct's correctness a thing an author has to remember, and gets it
wrong silently; a session that computes the stride cannot. *Also rejected:* a Metal-only struct
layout — the CPU writes these structs, so both mirrors must agree with the shader on all backends.

This task changes the bytes D3D12 shaders read. See "What could break".

**D5. `MTLSharedEvent` is the fence.**
It is monotonic, signalable from a command buffer, waitable from the CPU and from another queue's
command buffer — the four things `ICommandQueue` expresses. *Rejected:* `MTLFence` (intra-command-
buffer only, no value) and `MTLCommandBuffer` completion handlers (no cross-queue GPU wait, and it
would put the fence timeline behind a callback the RHI's `PollCurrentFenceValue` cannot poll).

**D6. Barriers stay no-ops for as long as every resource is declared with `useResource`.**
Metal hazard-tracks a resource it can see declared on an encoder, so `ICommandList::Barrier` has
nothing to do -- *provided* the declaration happens. Task 3 established that it is `useResource` that
makes this true, not the command buffer boundary: with `useResource` removed, two dispatches sharing
a compute encoder raced despite the FrameGraph's barriers, because the barriers were no-ops. The
no-op is therefore conditional, and it stops holding for a handle read out of a struct buffer, which
no `useResource` call covers. Task 6 has to make `Barrier` emit a real `memoryBarrier` at the same
time it introduces those handles.

**D7. `useResource` per bound handle, with a residency set later for the handles it cannot see.**
*Corrected in task 3, by measurement.* The plan first said a `MTLResidencySet` would **replace**
`useResource`. It does not: a residency set makes an allocation resident, and `useResource`
additionally declares the access, which is what Metal's hazard tracking orders dependent dispatches
by. Removing `useResource` in favour of a committed, per-command-buffer residency set left
`HistogramInstances` reading back zeros and `TransparentSort` unsorted — restoring `useResource`
alone fixed both, with the residency set still in place. Residency and usage are different guarantees
and Metal wants both.

So the command list keeps calling `useResource` for every handle it can enumerate, which is every
handle arriving through a `Uniforms` mirror. The residency set is still needed, but only for the
handles the list *cannot* enumerate — a `TextureHandle` sitting inside a `PbrMaterial` in a struct
buffer, which nothing on the CPU walks at bind time. It therefore lands with the descriptor seam in
task 6, where a test can show it is load-bearing; landing it here would have been a mechanism nothing
exercised. Residency sets need macOS 15, which the machine and the runner both have.

**D8. The command list opens encoders lazily and ends one when an incompatible command arrives.**
Metal has no free-form recording: a blit, a compute dispatch and a draw each need their own encoder,
and only one may be open. `ICommandList` therefore tracks which kind of encoder is open and closes it
on the first command that needs another — `SetMeshletState` opens a render encoder from the state's
`FrameBuffer`, `Dispatch` a compute encoder, `WriteBuffer`/`CopyBuffer` a blit encoder. Because the
FrameGraph already groups a pass's work, this reorders nothing.

**D9. Clears become load actions on the next render pass.**
Metal has no free-standing clear, so `ClearRtv`/`ClearDsv` record a pending clear against the
attachment and the next render encoder that binds it opens with `MTLLoadActionClear`. An attachment
cleared and never drawn to is flushed by an empty render pass at `Close`. *Rejected:* the old
backend's unconditional empty-render-pass-per-clear — correct, but it pays a full pass per
attachment per frame when the clear could have been free.

**D10. `D24S8` is remapped, not supported.**
Apple silicon has no `Depth24Unorm_Stencil8`, and the engine hardcodes `Format::D24S8` in
`ForwardPass.cpp:228`, `SkyboxPass.cpp:33` and — for D3D12 — `RenderTarget_d3d12.cpp:162,175`, which
the Metal target's own render target mirrors. The Metal backend maps
`D24S8` to `Depth32Float_Stencil8` in one place — its format conversion — so the DSV texture and the
pipeline's `dsvFormat` cannot disagree. *Rejected:* changing the three call sites to ask for `D32S8`
everywhere; it is a change to shared code to work around one backend's format table, and D3D12 would
then quietly pay for a wider depth buffer.

**D11. The editor hands `bgl` a `CAMetalLayer`, matching what SDL already does.**
`RenderTargetWindow` makes its widget layer-backed with a `CAMetalLayer` and passes the layer, so
`RenderTargetDesc::wnd` means one thing on macOS regardless of who created the window. *Rejected:*
teaching the backend to accept either an `NSView*` or a layer — a `void*` that is two types depending
on the caller is a crash waiting for the wrong caller.

**D12. Un-numbered interpolant semantics in the shared forward shaders.**
Slang's MSL emits `[[user(TEXCOORD0)]]` on a mesh output and `[[user(TEXCOORD)]]` on the matching
fragment input, and Metal's stage linkage rejects the mismatch. `forward/common.slang:14`,
`Skybox.slang:19-20` and `FullscreenRect.slang:4` use numbered semantics. Dropping the index matches
on both sides and is meaningless to D3D12, which binds by index-in-struct. *Rejected:* a Metal-only
shader arm — the drop-webgpu retrospective is explicit that per-backend shader arms are what made
that port shape the abstractions, and this is the same move. Worth filing upstream regardless.

**D13. No shader cache on Metal in this branch.**
`GraphicsOptions::shaderCacheDir` is configuration, not an interface (`docs/rhi.md`), so a backend
may ignore it. `MTLBinaryArchive` is the analogue and is worth having, but it is an optimisation on
a path that must first be correct. `ShaderCache_test` is skipped on Metal and the skip is named in
the test, not hidden in a filter.

**D14. Each task's gate is a named list of tests, which shrinks.**
`bgl_tests` cannot pass on the first slice and must not be landed failing. So a Metal build skips
what `libs/bgl/tests/metal_unsupported.txt` names and runs everything else, and every task removes
the ones it makes pass. The last task deletes the file. *Rejected:* the old `[metal]` tags — they
marked a test as being *about* Metal, which was never true, and a reader could not tell coverage
from intent. *Also rejected:* the inverse, a list of tests expected to **pass**. It reads more
naturally and it was what task 1 first shipped, but its default is wrong: a test added later is
silently not run on Metal until somebody remembers the file, and a regression in one outside the
list is invisible. A skip list makes coverage the default and opting out the deliberate act.

## The tasks, in order, and the gate for each

Order follows the dependency direction: nothing may be recorded before there is a list, nothing bound
before there is a resource, nothing drawn before there is a pipeline.

| | Task | Gate |
|---|---|---|
| 1 | **Revert 576ccee** and repair the 41 drift errors, minus the four hunks listed above. New `macos-clang-metal-debug/release` presets; widen the `RENDERER_BACKEND STREQUAL "DX12"` guards in `libs/bgl/CMakeLists.txt` and `libs/gamelib/CMakeLists.txt`; CI gains a Metal build leg, and stops running suites at all. | `libbgl.dylib` links; `Entry_test` creates a device; `metal_unsupported.txt` names what does not yet pass |
| 2 | Submission brought up to today's RHI: `MTLSharedEvent` as the timeline (D5), `Flush`, and the encoder state machine (D8) the restored `CommandList` predates. | a fence-ordering test: submit, CPU wait, cross-queue GPU wait, poll |
| 3 | Deferred destruction against N registered timelines, replacing task 1's never-reclaim placeholder. `WriteBufferSlice` and `CopyBuffer` needed nothing: the first is a non-virtual helper over `WriteBuffer`, the second landed in task 1. | `MultiQueueDeletion_test` — `MeshDelete_test` needs `Scene`, so it moves to task 6 |
| 4 | `IShader` + `IComputePipeline`: the restored `MetalPipelineReflection` reconciled with today's `ReflectedLayout`, and the `Kind::Resource` arm in `SlangReflection.cpp`. Finding 3 is already handled by the restored `MetalizeLayout`; this task verifies it still is. | `Uniforms_test`, `SlangSession_test`, and a hand-written `RWStructuredBuffer<uint>` dispatch read back texel-exact |
| 5 | idlgen's padding session, retargeted at Metal (D4). Four struct strides change on **both** backends. | generated `static_assert`s regenerate and compile on both presets; `assetlib_tests`; Windows CI compiles |
| 6 | `ResolveDescriptor` on `IResourceManager` and its six call sites (D3), plus the assertion pinning the no-caching invariant. Brings the residency set (D7) and real `memoryBarrier`s (D6) with it, because a handle inside a struct buffer is exactly what `useResource` cannot reach. | `MeshDelete_test`, and the engine's kernels still green: `HistogramInstances_test`, `CompactInstances_test`, `TransparentSort_test` |
| 7 | Textures and samplers: `WriteTexture` (the one `gunimplemented` left in the restored list), `GetTextureDesc`, deferred clears (D9) replacing the restored empty-pass clear, `D24S8` remap (D10). | `TextureSample_test`, `MaterialTextureDelete_test` |
| 8 | `IMeshletPipeline` and the render encoder: the restored per-stage MSL compilation carried to the engine's real forward shaders, render state, viewport/scissor, `DispatchMesh`. Un-numbered interpolants in the shared shaders (D12). | `MeshletRender_test`, `RenderGeometry` — the first golden image on Metal |
| 9 | The headless render target: frame ring, backbuffers, depth, motion vectors, `PresentAndAdvance`, `ResizeBackbuffers`, screenshot. | `PbrRender_test`, `Skybox_test`, `AlphaTest_test`, `Transparent_test`, `MotionVectors_test`, `Resize_test`, `Capture_test`, `MaterialOverrideRender_test` |
| 10 | The windowed target: `CAMetalLayer` swapchain, drawable acquisition, resize on layer bounds change. | `examples/bgl_window`, `bgl_sphere` and `bgl_two_windows` render live; screenshots in the PR |
| 11 | The remainder: `DispatchMeshIndirect`, the GPU debug buffer and assertion path, `SceneOverflow`, `TransparentDepthKeys`, `FrameGraph_test`. Delete `metal_unsupported.txt`. | `just test bgl_tests` fully green on Metal, with `--gpu-validation`'s Metal equivalent (API validation + shader validation) enabled |
| 12 | `gamelib` on macOS: widen its test guard, fix what a non-MSVC compiler and a case-sensitive path resolution turn up. | `just test gamelib` |
| 13 | The editor on macOS: Qt6, the layer-backed render widget (D11), the render thread, the thumbnail contexts. | `just test editor`, and the editor launches, loads a project and renders a scene — screenshot in the PR |

Tasks 1–4 and 7–9 are the backend proper. Tasks 5, 6 and 12–13 touch shared or downstream code, and
are the ones a reviewer should read hardest.

## What could break

**The D3D12 render path, in task 5, unverifiably.** Four GPU struct strides move. The generated
`static_assert`s move with them, so they agree by construction and prove nothing; a shader still
addressing a 52-byte `PbrMaterial` would compile, run, and draw the wrong thing. There is no D3D12
device on this machine and the Windows CI leg builds without running.

This was raised and the decision is to accept it: compile-only is the gate, and the D3D12 render path
carries unverified layout changes until someone runs `just test bgl_tests` on Windows. Recording it
here so that a later D3D12 rendering bug has an obvious first suspect. Two things make it cheaper than
it sounds — the same change was already made once in the opposite direction (#180) and reviewed, and
`git show 3b4ecc1` is the diff to invert.

**Golden images on a second backend.** Metal's rasterisation rules, blend precision and depth
resolution are not D3D12's, and task 8 is the first time a golden PNG is compared against a Metal
render. Expect the per-pixel tolerance to need widening, and expect the argument about whether that
weakens the D3D12 assertion — a separate golden set per backend is the fallback if one tolerance
cannot serve both honestly.

**Every suite is a local gate; CI compiles and runs nothing.** That is a deliberate call taken in
task 1 — the builds, not the tests, are what a CI run costs, and the suites are fast enough to be
worth more as something a person runs than as something a runner repeats. It does mean a regression
reaches `feat/metal-rhi` unless whoever pushes has run `just test`.

While CI did briefly run the Metal suite, it measured something worth keeping: **`macos-latest`'s GPU
is paravirtualised and cannot encode mesh shaders.** Compute dispatch, buffer and texture readback,
the cull kernels and transparent sort all pass there; the mesh path fails with
`-[AppleParavirtRenderCommandEncoder setMeshBytes:length:atIndex:]: unrecognized selector`. A
capability gap, not a wrong result. So if a CI leg ever runs this suite again, the three meshlet
tests have to come out of it, and no golden image that depends on the mesh path can be verified on a
runner.

**`libs/core`'s POSIX arm has not been compiled with a backend behind it.** `PLATFORM=MACOS` and
`src/posix` build today under `macos-clang-debug`, but nothing downstream exercises them; the crash
handler, file paths and threading get their first real use in this branch.

**A revert carries its assumptions back in with it.** Task 1 restores code written against an RHI with
one submission timeline, no residency sets and no `CopyBuffer`, and it will compile before it is
correct — a `Destroy*` given a `deferred` argument it ignores builds fine and frees a resource the GPU
is still reading. Tasks 2 and 3 exist to close that, and the reason `metal_unsupported.txt` (D14) starts
long rather than optimistically short is that a compiling revert is not a passing one.

## The prior work

The removed backend is at `576ccee7b57dec18b5f4539380eac593490dc7ed^`; its slice PRs (#56, #59, #60,
#64, #66, #68, #70) hold the review history, and the branch `backup/metal-backend` named in the
removal commit no longer exists on the remote — `git revert` against master's history is the recovery
path. The parts that carry the most value forward are `util_metal.cpp` (format conversion),
`MetalPipelineReflection.cpp` (the layout recomputation finding 3 demands) and
`MeshletPipeline_metal.cpp` (per-stage compilation, which is the fix for the mesh-shader linkage bug
in D12).
