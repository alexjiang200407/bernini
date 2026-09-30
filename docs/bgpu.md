# bgpu — the process's GPU device and the RHI every owner builds on

The process has one GPU device, and more than one library runs work on it: the renderer, and a
library that runs its own compute beside the frame — the crowd simulation's, the first
([crowdlib.md](crowdlib.md)).
`bgpu` is that device as an object of its own, with the debug layer that must precede it,
the Slang sessions that compile for it and the cache of what they compiled: the application creates
one and hands it to every owner. It is also the RHI each owner drives the device through -- a
device, queues, resource managers, pipelines and the buffer family of its own
([rhi.md](rhi.md)).

```cpp
auto desc             = bgpu::GpuContextDesc();
desc.enableDebugLayer = true;
desc.clientShaderDir  = projectShaders;                 // the client's modules, importable by name
desc.shaderCacheDir   = "shadercache";                  // every owner's compiled shaders; empty: none

auto context  = bgpu::CreateGpuContext(desc);         // the device, the debug layer, bgpu.log
auto graphics = bgl::CreateGraphics(context, gfxOpts);  // one owner

auto device = bgpu::CreateDevice(context);              // another: a compute client's own device
auto rm     = device->CreateResourceManager(bgpu::ResourceManagerDesc());
auto queue  = device->CreateCommandQueue(bgpu::QueueType::kCompute);
rm->RegisterQueue(queue.Get());
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
  the debug layer on, the context's destructor reports live objects into `bgpu.log`, so a leak is
  attributed to whichever owner made it rather than to "the device".
* **The program cache is the context's, because the sessions are.** What a key must describe —
  the compiler, the options every session is created with, every file under the search paths,
  every registered source module — is all held here, so `ProgramCache` keys and stores what any
  owner compiled, under `GpuContextDesc::shaderCacheDir`. The store keeps bytes: what an entry
  holds is its owner's format, and a `ProgramCacheOwner` tag and version in every key keep one
  owner from reading another's. It checks its own files — a header with the key and a hash of the
  payload — so a torn or misplaced entry is a miss before any owner decodes it. The file salt is
  one walk of the shader tree per context, taken by the first key; the source modules fold in at
  every key, so a text another owner changed moves it ([shader_cache.md](shader_cache.md)).
* **What deliberately does not.** Queues, allocators, resource managers, descriptor heaps,
  timestamp heaps, pipelines: each owner creates its own on the shared device, which is what keeps
  two owners isolated. The renderer's driver pipeline library — `pipelines.psolib`, the
  `MTL::BinaryArchive` — is one of them: it needs the native device, is dropped under GPU
  validation, and is serialized whole, so one writer per directory holds it
  ([shader_cache.md](shader_cache.md)). Metal's `.gputrace` capture is frame-scoped and stays in `Graphics`.
* **The RHI is here, and it is shared as classes, never as instances.** `IDevice`,
  `ICommandList`, `IResourceManager` and the rest are `bgpu`'s, and assume the engine's one bar:
  bindless resource access and a mesh stage ([rhi.md](rhi.md)). There is no lower tier to hide them
  from, so a compute client uses them rather than a copy of its own. Each owner calls
  `bgpu::CreateDevice` on the shared context and makes everything else from that device -- the list
  above is what keeps owners isolated. A resource manager is not tied to a frame loop: queues
  register themselves with it, and its owner calls `CleanupExpiredResources` when it likes. What
  the RHI cannot say portably its objects hand out as `GetNativeObject(NativeObjectType)` -- an
  untyped pointer, so no RHI header names a backend type ([rhi.md](rhi.md)). The context's device
  is also reachable without an `IDevice`, through
  [d3d12/native_device.h](../libs/bgpu/include/bgpu/d3d12/native_device.h) and
  [metal/native_device.h](../libs/bgpu/include/bgpu/metal/native_device.h), which forward-declare
  the native type rather than include the SDK.
* **Its Slang half is staged first.** The offset primitives (`idl.Entry`, `idl.Range` ...), the
  buffer family (`lib.types.EntryBuffer` ...) and the GPU assert channel (`lib.debug.dbg`,
  `idl.ErrorCode`, `idl.DebugRecord`) live under `libs/bgpu/shaders/src` and stage into the one
  `./shaders/src` every session resolves from, ahead of every tree that imports them; each module is
  checked against bgpu's tree alone. The sessions define `BERNINI_GPU_DEBUG` for every owner, so an
  owner whose kernels assert binds an assert buffer of its own (`bgpu::DebugBuffer`,
  `ICommandList::SetActiveDebugBuffer`); reading the records back is that owner's business.
* **Built as the renderer is.** It holds process-wide GPU state, so `BERNINI_RENDERER_LIBRARY_TYPE`
  decides its kind exactly as it does `bgl_extended`'s and `core_process`'s
  ([core_process.md § Linkage](core_process.md#linkage)). `BGPU_API` marks what is defined here
  and called from outside without a virtual call -- `CreateGpuContext`, `CreateDevice`, the native
  accessors, the error checkers, and the out-of-line members of the RHI's concrete classes (the
  cbuffer mirror, the growable and compute buffers, the pipeline batch); the interfaces cross the
  boundary through virtual calls. `bgpu_selfcheck` compiles each public header against `bgpu`
  alone, so none reaches into a renderer. On Metal it is
  also the one translation unit that emits metal-cpp's symbols, since it is the library every Metal
  user in the process links.

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
asked for being validation active. `[shadercache]`: no directory, no cache; a program compiled on a
miss and loaded on every later hit; two owners never sharing a key; a registered module moving
every key; a key stable across contexts until a file under the search paths changes; a torn,
altered or misplaced entry missed. `bgl_extended_tests` `[device]` covers the renderer as an owner:
a renderer built on a context, a second renderer on the same context, two renderers binding one
surface directory once, a source module re-registered under its name replacing the text with the
cache key following, and a second owner compiling through the sessions the renderer dropped.
