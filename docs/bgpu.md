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
auto rm     = device->CreateResourceManager(bgpu::ResourceManagerDesc::ComputeOnly());
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
  `IGraphics` neither creates nor lends a device: `bgl` stays a rendering contract.
* **What lives here is what must precede the device or be shared through it.** The D3D12 debug
  layer, GPU-based validation, the DXGI and D3D12 info queues and the callback that routes their
  messages into the log, the PIX capturer load, Metal's validation-from-environment detection, the
  Vulkan instance with its validation layer and messenger ([§ Vulkan](#vulkan)), the
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
* **The D3D12 pipeline states every owner built.** `FindPipelineState` / `SharePipelineState` in
  [native_device_d3d12.h](../libs/bgpu/src/d3d12/native_device_d3d12.h), private to the backend, hold PSOs for the
  context's life, keyed by the root signature and the owner's identity for everything else. A PSO
  is immutable and the device's, so a second renderer on the context reuses the first one's
  instead of building its own. It matters most under GPU-based validation, which patches each new
  PSO object on first use: that is most of a validated test suite's time, and without sharing it
  is paid again by every case. Metal renderers still build their own.
* **What deliberately does not.** Queues, allocators, resource managers, descriptor heaps,
  timestamp heaps: each owner creates its own on the shared device, which is what keeps two owners
  isolated. The renderer's driver pipeline library — `pipelines.psolib`, the
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
  untyped pointer, so no RHI header names a backend type ([rhi.md](rhi.md)). That is the only way
  out to the native device: no public header hands it out without an `IDevice`. A buffer crosses
  between owners the same way: the producer reads its native object (`GetNativeBuffer`) and the
  consumer imports it into its own manager (`ImportNativeBuffer`), each ordering its use against
  the other's queue on the GPU ([rhi.md](rhi.md)).
* **Its Slang half is staged first.** The offset primitives (`idl.Entry`, `idl.Range` ...), the
  buffer family (`lib.types.EntryBuffer` ...) and the GPU assert channel (`lib.debug.dbg`,
  `idl.ErrorCode`, `idl.DebugRecord`) live under `libs/bgpu/shaders/src` and stage into the one
  `./shaders/src` every session resolves from, ahead of every tree that imports them; each module is
  checked against bgpu's tree alone. The sessions define `BERNINI_GPU_DEBUG` for every owner, so an
  owner whose kernels assert binds an assert buffer of its own (`bgpu::DebugBuffer`,
  `ICommandList::SetActiveDebugBuffer`); reading the records back is that owner's business.
* **Built as the renderer is.** It holds process-wide GPU state, so `BERNINI_RENDERER_LIBRARY_TYPE`
  decides its kind exactly as it does `bgl`'s and `core_process`'s
  ([core_process.md § Linkage](core_process.md#linkage)). `BGPU_API` marks what is defined here
  and called from outside without a virtual call -- `CreateGpuContext`, `CreateDevice`, the native
  accessors, the error checkers, and the out-of-line members of the RHI's concrete classes (the
  cbuffer mirror, the growable and compute buffers, the pipeline batch); the interfaces cross the
  boundary through virtual calls. `bgpu_selfcheck` compiles each public header against `bgpu`
  alone, so none reaches into a renderer. On Metal it is also the one translation unit that emits
  metal-cpp's symbols, since it is the library every Metal user in the process links.
* **The context never includes the RHI.** The RHI depends on the context and never the reverse, so
  a consumer of the context alone could take it by a CMake split, with no code moved.
  `bgpu_context_selfcheck` holds that: it stages the context's headers (`BGPU_CONTEXT_HEADERS` in
  `libs/bgpu/CMakeLists.txt`) into a tree of their own and compiles each against it, so an include
  of an RHI header fails to resolve. A new context header joins that list.

## Minimum system requirements

The engine has one hardware bar, bindless resource access plus a mesh stage, and no lower tier.
`CreateGpuContext` checks the machine against it as soon as it has the native device, before any
owner builds anything on it. Below the bar it throws `bgpu::UnsupportedSystem`
([SystemRequirements.h](../libs/bgpu/include/bgpu/SystemRequirements.h)) instead of letting the
first mesh pipeline fail. Every client creates its device there, so the editor, every game and
every test get the check without asking for it, and nothing can turn it off.

| | Needs | Checked as |
|---|---|---|
| macOS | a Mac with Apple silicon (M1 or later) | `hw.optional.arm64`, so an x86_64 build under Rosetta passes |
| | macOS 13 Ventura or later | `NSProcessInfo.operatingSystemVersion` |
| | a Metal 3 GPU with a mesh stage and bindless | `MTLGPUFamilyMetal3`, `MTLGPUFamilyApple7`, `MTLArgumentBuffersTier2` |
| Windows | a D3D12 device at feature level 12_0 | `D3D12CreateDevice` on DXGI's first adapter, the one every owner draws on |
| | a mesh stage and bindless | `MeshShaderTier` 1 (`OPTIONS7`), `ResourceBindingTier` 3 (`OPTIONS`) |
| | a driver new enough for the shaders and barriers | shader model 6.6, the profile the sessions compile to; `EnhancedBarriersSupported` (`OPTIONS12`) |
| Vulkan | a Vulkan device | the first physical device the loader enumerates, the system's preferred one |
| | a driver at Vulkan 1.3 | `VkPhysicalDeviceProperties::apiVersion`; below it nothing else is checked, since an old driver hides what the GPU can do |
| | a mesh stage and bindless | `VK_EXT_mesh_shader` with its `meshShader` and `taskShader` features, the second being D3D12's amplification stage; the descriptor-indexing features a runtime array of sampled images, storage images or storage buffers needs |
| | a driver that lays a buffer out as the shaders declare it | `scalarBlockLayout`, what `ScalarDataLayout` compiles to in SPIR-V |
| | a driver with the fences and barriers the RHI is written in | `timelineSemaphore` and `synchronization2`, D3D12's fence values and enhanced barriers; both mandatory in 1.3, so a driver that hides them is told to update |
| | a driver with one bindless array for buffers and textures | `VK_EXT_mutable_descriptor_type`: Slang lowers a buffer handle and a texture handle to the same binding, as D3D12 indexes one CBV/SRV/UAV heap; a mesh-shading GPU without it has an old driver |
| | the graphics RHI's draws and states | `dynamicRendering`, `drawIndirectCount`, and the core features D3D12's blend, raster and sampler states can ask for: `independentBlend`, `dualSrcBlend`, `fillModeNonSolid`, `depthClamp`, `multiViewport`, `samplerAnisotropy`, `samplerFilterMinmax`, `samplerMirrorClampToEdge`, `textureCompressionBC` |

An Intel Mac is refused even when its GPU supports Metal 3: the engine is built and tested on Apple
silicon only.
On Windows that is NVIDIA Turing (GTX 1660, RTX 2060) and newer, AMD RDNA2 (Radeon RX 6000) and
newer, and Intel Arc. A machine whose first adapter is an integrated GPU without mesh shaders is
refused, even when a second GPU would pass, because the engine does not choose an adapter.

* **The error is for a player, and the data is for the client.** `what()` says that this computer
  does not meet the minimum requirements. It then names each thing to replace or update once, with
  what the machine has instead, in English. `Unmet()` is the same list as `Requirement`s, so a
  client can write its own words or localize them. bgpu never shows UI: it is linked by headless
  tests and tools. The editor shows the text in its "could not start" dialog. A game must show it
  itself, because a Steam player never sees stderr and Steam cannot enforce hardware requirements.
  `core::show_fatal_message` (`core/platform/util.h`) is the native dialog for a game's `main` to
  call in its catch.
* **The checks are separate from the reading.** Each backend reads plain facts off its device
  (`AppleSystemFacts`, `D3d12SystemFacts`, `VulkanSystemFacts`), and `CheckSystemRequirements`
  decides on them. So `bgpu_tests` `[sysreq]` pins every backend's checks on any machine, including
  the machines that fail them, which no test machine is.

## Vulkan

`RENDERER_BACKEND=VULKAN` is a third backend being brought up on Windows ahead of the Linux build
that needs it: **the context and the whole RHI**, every case of `bgpu_tests` included. Nothing
above `bgpu` is built — no renderer, no crowd libraries, no editor; the renderer's backend half and
its swapchain are the next step. D3D12 stays the Windows default; only the
`windows-clang-vulkan-debug` preset selects this.

* **The first physical device, as D3D12 takes DXGI's first adapter.** The loader sorts what it
  enumerates — by the adapter order Windows prefers, and on Linux discrete before integrated — so
  the engine still does not choose between GPUs: the machine's settings do.
* **The device is created with what the minimum requirements name, and no other feature.** One list
  in [GpuContext_vulkan.cpp](../libs/bgpu/src/vulkan/GpuContext_vulkan.cpp) is both what is read
  off the physical device and what is enabled on the logical one, so a requirement that is checked
  is one an owner can use. A feature the RHI comes to need is added there and to the requirements
  together.
* **It is created with every queue of every family.** A Vulkan device's queues are fixed when it
  is made, and here each owner takes queues of its own afterwards, so the context cannot know how
  many will be asked for. Asking for all of them leaves that to the RHI.
* **volk, not the loader's import library.** The function pointers are filled when the context is
  created and cleared when it is destroyed, so nothing links `vulkan-1` and a machine with no
  Vulkan driver starts and is refused with `UnsupportedSystem`. It also means no Vulkan call is
  valid without a live context. The handles are `GetVulkanHandles`
  ([native_device_vulkan.h](../libs/bgpu/src/vulkan/native_device_vulkan.h)), private to the
  backend as the D3D12 device is.
* **The validation layer comes with the build.** `enableDebugLayer` enables
  `VK_LAYER_KHRONOS_validation`, which vcpkg builds and `bgpu`'s build stages beside the
  executables, as the Agility SDK's debug layer is — so it needs no Vulkan SDK on the machine and
  is the same version everywhere. The loader does not search an executable's directory, so the
  context adds it through `VK_ADD_LAYER_PATH` in its own environment before creating the instance;
  a value already set there is left alone. Asked for and not found, the context throws rather than
  running unvalidated. The loader ignores that variable in an elevated process.

  The layer is built from an overlay port (`cmake/ports/vulkan-validationlayers`) for one reason:
  vcpkg's links a shared mimalloc, whose redirect DLL replaces the C runtime's allocator for the
  whole process as it loads. Enabling a debug layer must not change the allocator under the
  engine, so the overlay builds the layer without it, slower and self-contained in one DLL.
* **Every message goes to `bgpu.log`; only the validation layer's are strict.** A debug-utils
  messenger writes each message as `[Vulkan] ...` at its own severity, and `logLevel` filters them
  as it does everything else. `strictError` ends the process on a warning or error of the
  validation type only: the loader reports other software's broken layers and drivers as general
  warnings and errors, which are the machine's and not this process's. An overlay or injector whose
  implicit layer fails to load leaves an `[error] [Vulkan] Failed to open dynamic library` line in
  the log at every context; it is not a finding.
* **An object that outlives the context is named by the layer.** Destroying the device with a
  child still alive is a validation error naming the object, raised while the messenger is still
  attached, so it reaches the log — and ends the process under `strictError` — as D3D12's
  live-object report does.
* **`enableGPUValidationLayer` is GPU-assisted validation**, switched on through the layer's
  settings when the instance is created. Unlike D3D12's it is the instance's, so it ends with the
  context.
* **The sessions compile to SPIR-V**, at the same profile as every other backend, packing buffers
  as FXC does (`ForceDXLayout`): the C++ mirrors `bgpu_idlgen` generates are D3D12's layout on
  every backend but Metal, and std140 would move a constant-buffer member after an 8-byte handle to
  the next 16-byte row. A bindless handle becomes an index into a descriptor array of runtime size,
  which is the descriptor-indexing requirement, in set 1 (`BindlessSpaceIndex`); constant buffers
  are set 0.

The RHI keeps D3D12's semantics wherever Vulkan's are stricter, since every owner was written
against D3D12 and Metal:

* **A fence value is a timeline semaphore's value**, and a barrier is synchronization2's, which is
  why both are in the bar. Vulkan waits only inside a submission, so `InsertWait*` holds the wait
  until the queue's next `ExecuteCommandList` or `Flush`.
* **Every recording opens with a full memory barrier.** D3D12 decays every resource's state between
  `ExecuteCommandLists` calls; Vulkan orders nothing between two submissions on one queue.
* **Buffers and images are shared by every queue family** (concurrent sharing): D3D12 has no
  queue-family ownership to transfer.
* **Every texture is in `VK_IMAGE_LAYOUT_GENERAL`**, whatever layout a barrier names but
  `kUndefined`. D3D12 runs a copy, an SRV read or a clear in more than one layout and never says
  which a texture is in, where Vulkan needs it at every copy, clear, attachment and descriptor; in
  the one layout valid for all of them, a descriptor written once is right for every read and a
  barrier keeps D3D12's meaning — its syncs and accesses, and a discard from `kUndefined`. On a
  driver with `VK_KHR_unified_image_layouts` it costs nothing. `kPresent` is a swapchain's and ends
  the process until there is one.
* **A clear is `vkCmdClearColorImage` or `vkCmdClearDepthStencilImage`** over the view's
  subresources: in `GENERAL` it needs no render pass. An integer target's clear converts D3D12's
  floats to its own type. A render target view of a 3D texture names a range of its depth slices,
  reached as the layers of a 2D-array view, so such a texture is made 2D-array compatible.
* **A mesh dispatch draws inside dynamic rendering, opened lazily.** D3D12 binds targets and
  draws; Vulkan draws between `vkCmdBeginRendering` and `vkCmdEndRendering`. The first mesh
  dispatch begins rendering on its meshlet state's frame buffer, loading and storing every
  attachment as D3D12 keeps them; draws on that same frame buffer stay inside it, and every other
  command ends it first. A meshlet pipeline is made for dynamic rendering into its desc's formats,
  with its viewports and scissors, any number of each, set per draw.
* **D3D's clip space, through a negative-height viewport.** Every viewport is flipped — origin at
  its bottom edge, height negative — which mirrors D3D's +y-up clip space into Vulkan's +y-down, so
  the same SPIR-V draws the image D3D12 draws and D3D12's winding rule carries over unchanged.
  Slang's `-fvk-invert-y` would flip only the positions a mesh stage writes, and change the program
  cache's contents. Depth clip off is depth clamp on. An indirect dispatch reads D3D12's argument
  layout, which is `VkDrawMeshTasksIndirectCommandEXT`; a count dispatch draws at most one, as
  D3D12's `ExecuteIndirect` here does. The stencil reference is baked into the pipeline, so unlike
  D3D12 it is honoured.
* **A new texture reaches `GENERAL` at its first submission.** An image is born `UNDEFINED`, so the
  manager keeps the transitions of the textures it made, and the queue that next submits a list
  recorded against it submits them first, from a command buffer of its own: the list's allocator
  may be recording another list by then. A transition taken at `Open` instead would race a list
  submitted ahead of one opened earlier.
* **Queues are the context's.** A queue of a type is the one the fewest owners hold among the
  families the type prefers — a compute family without graphics for compute, a transfer-only one
  for copies — spilling to another family before two owners share a `VkQueue`. Owners on one queue
  submit under that queue's lock in the context. One with a GPU wait on a later submission of its
  own can stall, which is why sharing comes last.
* **Every buffer and every texture view is a descriptor in its manager's bindless table**, binding 2
  of set 1, where Slang's SPIR-V reads a buffer handle and a texture handle alike. The binding is a
  mutable array of storage buffers and sampled images (`VK_EXT_mutable_descriptor_type`, in the
  bar), so buffers and views take indices from one space, as D3D12's CBV/SRV/UAV heap gives them.
  Samplers are binding 0, indexed by their pool slot as D3D12's sampler heap is. Every table has
  the same fixed-size layout (`BindlessTable::c_Capacity`, `c_SamplerCapacity`), so any device's
  pipeline layout is compatible with any manager's set. A second view of a buffer
  (`CreateBufferSrv`, `CreateBufferUav`) is a second descriptor onto it: a storage-buffer
  descriptor has no stride.
* **A depth texture's view reads one aspect of the image's own format**: stencil for `X24G8_UINT`
  and `X32G8_UINT`, depth for anything else, `R32_FLOAT` included. A colour texture is made
  format-mutable, so its view may read another format of its class as a D3D12 typeless texture's
  does. `D24S8` is `D32_SFLOAT_S8_UINT`, as Metal makes it `Depth32Float_Stencil8`: AMD has no
  24-bit depth.
* **A sampler's border is transparent black, opaque black or opaque white**, the three Vulkan has
  without `VK_EXT_custom_border_color`; any other ends the process. No caller sets one.
* **A constant buffer is a uniform-buffer descriptor written per dispatch**, its bytes in the
  list's upload ring as on D3D12 and its set from the command allocator, which is a command pool
  per queue family and resets with both.
* **One allocation per buffer and per texture**, as D3D12 commits one resource for each. A
  renderer's thousands of textures are what would reach `maxMemoryAllocationCount` and need a
  suballocator.
* **A texture's readback rows are 256-byte aligned**, as D3D12's footprint is, rounded up to a whole
  number of blocks, so `GetTextureReadbackLayout` reads the same on both.
* **An exported buffer is a `VkBuffer`** (`NativeObjectType::kVkBuffer`). Vulkan counts no
  references to one, so its memory is ref-counted and found by `VkBuffer` in a process-wide
  registry: an import keeps it alive past the producer's release, and an import of a buffer no
  manager made is refused. **An exported texture is a `VkImage`** (`kVkImage`), held the same way
  (`ImageMemory`) when a manager made it; one no manager made — a swapchain's — is borrowed, since
  nothing can add a reference to it, and its maker keeps it alive while the handle lives.
* **The program cache holds SPIR-V and reflection**; there is no driver pipeline cache yet.

`bgpu_tests` `[vulkan]` pins the device and its queues, the bindless and synchronization features it
was created with, a validation message in the log, a leaked object named in it and a kernel compiled
to SPIR-V; then each backend class on its own: the manager's descriptors and deferred frees, an
import outliving its producer, a constant buffer at D3D12's offsets, a pipeline from its cached
program, a recording read back, a kernel dispatched, a wait between two queues, how owners spread
over the queues, and a timed span. `TextureRoundTrip_test` runs on every backend: a texture written
and read back, one a kernel samples through its view and a sampler, and a colour and a depth target
read back as what they were cleared to. `MeshDispatch_test` pins which way up a mesh dispatch draws
and how many groups an indirect one runs, a count of zero included.

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
altered or misplaced entry missed. `bgl_tests` `[device]` covers the renderer as an owner:
a renderer built on a context, a second renderer on the same context, two renderers binding one
surface directory once, a source module re-registered under its name replacing the text with the
cache key following, and a second owner compiling through the sessions the renderer dropped.
