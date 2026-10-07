# bgpu

The process's GPU device, the Slang sessions that compile for it, and the RHI every owner of the
device builds on — the renderer (`bgl`) and a compute client beside it (`crowdlib`). What
each part is for and why it lives here is [docs/bgpu.md](../../docs/bgpu.md); how the RHI is used is
[docs/rhi.md](../../docs/rhi.md).

- CMake target: bgpu, built as the renderer is (`BERNINI_RENDERER_LIBRARY_TYPE`). `BGPU_API` marks
  every function defined here that a consumer calls without a virtual call; a new one needs it, or a
  shared Windows build fails to link.
- Public headers under `./include/bgpu`, namespace `bgpu`. The RHI's interfaces and plain-old-data
  descriptors are there (`cmd/`, `device/`, `pipeline/`, `resource/`, `uniforms/`, `types/`,
  `buffer/`); their backend implementations are under `./src/d3d12` and `./src/metal`, one per
  binary. `./src/vulkan` is a third backend's context, with no RHI yet. Nothing outside
  `src/<backend>` includes a backend header (d3d12, metal-cpp or volk) — what a
  caller needs of a backend it asks for with `GetNativeObject(NativeObjectType)` / `GetNativeTexture`,
  an untyped `NativeObject`, so no RHI header names a backend type.
- **Nothing here names the renderer.** `bgpu_selfcheck` compiles every public header alone, with no
  PCH, against `bgpu` only, so an `#include <bgl/...>` — or an include a header
  leaned on the PCH for — stops the build. `bgpu_check_shaders` does the same for every Slang module
  under `./shaders/src`.
- **The context never includes the RHI.** `bgpu_context_selfcheck` compiles the headers listed in
  `BGPU_CONTEXT_HEADERS` against those headers alone; a new context header joins the list.
- Error handling: `core::ensure` for internal problems; throw for the caller's.
- Verification: `just test bgpu` — `[compute]` is an owner with no renderer in the process — then the
  renderer's suite, `bgl_tests`, which drives the same RHI harder. The cases under
  `./tests/src/context` need a context and no RHI; a backend with no RHI builds those alone.

## D3D12 (`./src/d3d12`)

- PCH is `./src/d3d12/pch.h`. Don't `#include` the headers in it.
- HRESULTs: `D3D12CreateDevice(...) >> d3d12ErrChecker;` — `bgpu::d3d12ErrChecker`, from
  `<bgpu/d3d12/D3d12ErrorChecker.h>`, in the PCH.
- Implementation files take a `_d3d12` suffix: `IDevice` is implemented by `Device_d3d12.cpp`.
- **A target that compiles shaders needs `dxcompiler.dll` and `dxil.dll` beside the executable.**
  Slang loads both with `GetProcAddress`, so nothing imports them and vcpkg's applocal deployment
  does not stage them. bgpu's build stages them, with the Agility SDK's `D3D12Core.dll`, beside
  the executables, so linking bgpu is enough; an executable also links `bgpu_d3d12_agility`, the
  SDK's two exports, which must be its own.
- GPU-based validation is opt-in through `GpuContextDesc::enableGPUValidationLayer`; the debug layer
  is separate and stays on with `enableDebugLayer`.

## Metal (`./src/metal`)

- PCH is `./src/metal/pch.h`. Don't `#include` the headers in it.
- Implementation files take a `_metal` suffix.
- Metal is reached through **metal-cpp**, pulled by `./CMakeLists.txt` with `FetchContent` at a
  pinned commit and carried on bgpu's public include path. Its out-of-line symbols are emitted once
  per process by `./src/metal/MetalImpl.cpp`, so nothing else may define the
  `*_PRIVATE_IMPLEMENTATION` macros.
- Error handling: Metal signals failure by returning nil and fills its `NSError` only *sometimes*.
  `MetalErrorChecker` (`<bgpu/metal/MetalErrorChecker.h>`, in the PCH) holds the error so a call site
  reads like its D3D12 counterpart: `library.get() >> errChecker;`. Where a call takes no error
  out-param — most of them — a `core::ensure` on the returned pointer is the whole check.
- **Shaders are compiled at runtime** from the staged Slang sources, to MSL via
  `newLibraryWithSource`, so a shader error surfaces when the pipeline is first built.
- **A scope that creates an autoreleased Metal object owns the pool it drains into.** Most Metal
  factories autorelease — `commandBuffer()`, `nextDrawable()` — and the pool the object lands in is
  whichever one on this thread was pushed last. So every entry point here that makes, submits or
  releases one holds its own pool (`ScopeAutoreleasePool()`, `src/metal/autorelease_scope.h`) — the
  device's factories, the resource manager's creates and destroys, the queue's submit and waits,
  `CommandList::Open`..`Close` — and a caller needs no pool of its own: the renderer's net
  (`AutoreleaseNet_metal.h`, in `bgl`) catches nothing from here, and a compute client with
  no net leaks nothing. A forgotten one is silent, so `scripts_tests` runs all of `bgpu_tests` --
  the RHI with no renderer, `RhiEntryPoints_test` reaching every factory -- under
  `OBJC_DEBUG_MISSING_POOLS=YES`, and fails on anything of ours autoreleased with no pool. A new
  entry point gets a line in that case. What Metal itself
  autoreleases on its own completion threads (the device, as a finished command buffer releases
  the resources it held) is outside any code here, and harmless: the device is never freed.
  Committing a command buffer before the pool drains is safe: the driver holds its own reference
  until the buffer retires.
- **A flush is not done when its fence is.** An event signalled with `encodeSignalEvent` fires as
  the GPU passes it, and the driver goes on retiring the command buffer and releasing what it held
  for a while after — measurably, most flushes. Anything that frees a resource because "the GPU is
  idle" needs the buffer *retired*, so `CommandQueue::Flush` ends on `waitUntilCompleted`, which
  submission order extends to everything committed before it. A deferred free needs no such thing:
  its gate only drops our reference, and the in-flight buffer still holds its own.
  `bgpu_tests "[teardown]"` pins both rules.
- **GPU validation comes from the environment**, not a flag:

  ```bash
  METAL_DEVICE_WRAPPER_TYPE=1 just run bgpu_tests                        # API validation
  METAL_DEVICE_WRAPPER_TYPE=1 MTL_SHADER_VALIDATION=1 just run bgpu_tests  # + GPU validation
  ```

## Vulkan (`./src/vulkan`)

The context only: `CreateGpuContext` is defined and `CreateDevice` is not, so nothing above `bgpu`
is built on this backend ([docs/bgpu.md § Vulkan](../../docs/bgpu.md#vulkan)).

- PCH is `./src/vulkan/pch.h`. Implementation files take a `_vulkan` suffix.
- Vulkan is reached through **volk**: include `"volk_vulkan.h"`, never `<vulkan/vulkan.h>`, whose
  prototypes name symbols nothing links. The function pointers are defined once per process by
  `./src/vulkan/VolkImpl.cpp`, loaded by the context and cleared when it is destroyed, so no Vulkan
  call is valid without a live context.
- Error handling: creating the context throws — `UnsupportedSystem` for a machine below the bar,
  `std::runtime_error` for a fault, with the `VkResult` named by `string_VkResult`.
- The validation layer is staged beside the executables by this library's build, and the context
  points the loader at it. A new instance or device extension, feature or layer setting is enabled
  in `GpuContext_vulkan.cpp`, the one place either object is created.
- `just build --preset windows-clang-vulkan-debug`, then
  `just test bgpu --no-build --build-dir build/ninja-clang-vulkan-debug`: the suite builds and runs
  the configured preset's unless it is told otherwise.

## Shaders (`./shaders`)

- `./shaders/src` is the RHI's Slang half — the IDL offset primitives (`idl.Entry`, `idl.Range` …),
  `idl.RawRange`, `idl.RecordHeader`, `idl.RawArena`, `idl.ErrorCode` and `idl.DebugRecord`, the buffer family (`lib.types.*Buffer`, the raw arena's `RawHandleView` / `RawHandleArena`, and `lib.types.BoxedHandle` for a buffer of handles) and the GPU assert
  channel (`lib.debug.dbg`) — staged into `./shaders/src` beside the executables by
  `bgpu_copy_shaders`, first of every copier, since every other tree imports it. An import name is the
  path under that root, so a module here keeps its name wherever it is staged from.
- Its C++ mirrors: the hand-written offset primitives under `include/bgpu/idl`, and the modules with a
  concrete layout listed in `BGPU_IDL_CPP_SOURCES`, generated per backend as `bgpu::idl`
  ([docs/idlgen.md](../../docs/idlgen.md)).
- `./shaders/tests/bgpu` is `bgpu_tests`' own kernels, staged under `./shaders/tests/bgpu` and
  imported as `bgpu.<Name>`.
