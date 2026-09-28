# bgpu — the process's GPU device, owned by no renderer

The process has one GPU device, and more than one library runs work on it: the renderer, and a
library that runs its own compute beside the frame — the crowd simulation's, the first.
`bgpu` is that device as an object of its own, with the debug layer that must precede it
and the Slang sessions that compile for it: the application creates one and hands it to every owner.

```cpp
auto desc             = bgpu::GpuContextDesc();
desc.enableDebugLayer = true;
desc.clientShaderDir  = projectShaders;                 // the client's modules, importable by name

auto context  = bgpu::CreateGpuContext(desc);         // the device, the debug layer, bgpu.log
auto graphics = bgl::CreateGraphics(context, gfxOpts);  // one owner
// a compute client is another: it takes the same context
```

## Design Choices

* **The application owns the device; every library borrows it.** `bgpu::GpuContext` holds the
  one strong reference to the native device, so the device dies with the context's last holder.
  **One is live per process at a time**: D3D12 hands back one device per adapter however often it is
  asked, and enabling the debug layer once that device exists removes it, so a second context could
  never be independent of the first. `CreateGpuContext` refuses while one is live, on Metal too,
  so a program is portable; another may follow once the last holder has dropped it.
  `IGraphics` neither creates nor lends a device: `bgl` stays a rendering contract, and a renderer
  that has no native device to offer — `bgl_wgpu`, should it exist — is not asked for one.
* **What lives here is what must precede the device or be shared through it.** The D3D12 debug
  layer, GPU-based validation, the DXGI and D3D12 info queues and the callback that routes their
  messages into the log, the PIX capturer load, Metal's validation-from-environment detection, the
  device itself, the `bgpu.log` file, and the Slang sessions
  ([src/SlangSessions.h](../libs/bgpu/src/SlangSessions.h)) with their search paths. With
  the debug layer on, the context's destructor reports live objects, so a leak is attributed to
  whichever owner made it rather than to "the device".
* **What deliberately does not.** Queues, allocators, resource managers, descriptor heaps,
  timestamp heaps, pipelines: each owner creates its own on the shared device, which is what keeps
  two owners isolated. The renderer's shader cache stays the renderer's — its program layer stores
  RHI-shaped blobs, and its salt and `pipelines.psolib` make a cache directory single-writer
  ([shader_cache.md](shader_cache.md)). Metal's `.gputrace` capture is frame-scoped and stays in
  `Graphics`.
* **The RHI is not here.** `IDevice`, `ICommandList`, `IResourceManager` and the rest are
  `bgl_extended`'s and assume its GPU-driven bar. An owner other than the renderer reaches the
  device through the backend header — [d3d12/native_device.h](../libs/bgpu/include/bgpu/d3d12/native_device.h),
  [metal/native_device.h](../libs/bgpu/include/bgpu/metal/native_device.h) —
  and drives the API itself.
* **Built as the renderer is.** It holds process-wide GPU state, so `BERNINI_RENDERER_LIBRARY_TYPE`
  decides its kind exactly as it does `bgl_extended`'s and `core_process`'s
  ([core_process.md § Linkage](core_process.md#linkage)). `BGPU_API` marks the few exports —
  `CreateGpuContext`, the two native-device accessors, the D3D12 error checker and the Slang error
  checker; everything else crosses the boundary through virtual calls. On Metal it is
  also the one translation unit that emits metal-cpp's symbols, since it is the library every Metal
  user in the process links.

## Interface Index

| Symbol | File | Role |
|---|---|---|
| `GpuContextDesc`, `LogLevel` | [include/bgpu/GpuContext.h](../libs/bgpu/include/bgpu/GpuContext.h) | The device-level options: `enableDebugLayer`, `enableGPUValidationLayer`, `enablePixDebug`, `strictError`, `logLevel`, `clientShaderDir` |
| `GpuContext` | same | The context: the desc, whether validation is running, and the Slang compiler — search paths, source modules, `LoadModule`, `LoadScalarLayoutModule`, `ReleaseSlangSessions`. Surface reflection is the renderer's, on the module the scalar-layout load returns |
| `SlangErrorChecker` | [include/bgpu/SlangErrorChecker.h](../libs/bgpu/include/bgpu/SlangErrorChecker.h) | `result >> checker`: the one Slang failure check, for every owner that compiles |
| `SlangSourceModule` | same | A module given as text under an import name |
| `GetSourceSalt` | same | The order-independent fold of every registered module, name and text; an owner's shader cache mixes it into each key |
| `CreateGpuContext` | same | The one factory, defined by the backend the build selected |
| `GetD3d12Device`, `GetMtlDevice` | `include/bgpu/{d3d12,metal}/native_device.h` | The native device behind a context, borrowed |
| `d3d12ErrChecker` | [include/bgpu/d3d12/D3d12ErrorChecker.h](../libs/bgpu/include/bgpu/d3d12/D3d12ErrorChecker.h) | `hr >> d3d12ErrChecker`: the one HRESULT check, shared with `bgl_d3d12` |

## Threading & Synchronization

* **Sessions are per thread.** A thread's first compile creates its own global session and session,
  and a `slang::IModule` belongs to the session of the thread that loaded it.
* **A source module's name maps to one text per context.** `AddSourceModule` of a name already
  registered replaces the text; of the same text again does nothing, so two renderers binding the
  same surfaces cost one registration and no session drop. A changed text re-points every owner's
  later compiles, and every owner's cache key follows it through `GetSourceSalt`; a layout an owner
  reflected from the old text does not, so a text changes only before the owners that read it exist.
* **`ReleaseSlangSessions` and a new or changed `AddSourceModule` drop every thread's session, for every owner.**
  Their precondition is therefore shared: no compile in flight and no `slang::` object held, by any
  owner of the context. The renderer already satisfies it — it compiles lazily through `LoadModule`
  and holds nothing across calls — and every other owner must.
* **Teardown.** Each owner drains the queues it created before dropping its context reference.
  Nothing flushes "the device": no owner has all of its queues.
* **Metal autorelease pools are a per-thread stack**, so an owner scopes a pool around anything that
  autoreleases and never holds one for its whole life: two long-lived pools on one thread can only
  die in reverse order of creation. The renderer's long-lived net for strays is its own
  (`bgl_metal`'s `AutoreleaseNet`), shared between the renderers on a thread; `bgpu` keeps no pool.

## Verification

`bgpu_tests`: one live context per process and its successor, the search-path order, a released session
recreated by the next load, a source module seen by a thread that never compiled, and validation
asked for being validation active. `bgl_extended_tests` `[device]` covers the renderer as an owner:
a renderer built on a context, a second renderer on the same context, two renderers binding one
surface directory once, a source module re-registered under its name replacing the text with the
cache key following, and a second owner compiling through the sessions the renderer dropped.
