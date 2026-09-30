# bgpu

The process's GPU device, the Slang sessions that compile for it, and the RHI every owner of the
device builds on — the renderer (`bgl_extended`) and a compute client beside it (`crowdlib`). What
each part is for and why it lives here is [docs/bgpu.md](../../docs/bgpu.md); how the RHI is used is
[docs/rhi.md](../../docs/rhi.md).

- CMake target: bgpu, built as the renderer is (`BERNINI_RENDERER_LIBRARY_TYPE`). `BGPU_API` marks
  every function defined here that a consumer calls without a virtual call; a new one needs it, or a
  shared Windows build fails to link.
- Public headers under `./include/bgpu`, namespace `bgpu`. The RHI's interfaces and plain-old-data
  descriptors are there (`cmd/`, `device/`, `pipeline/`, `resource/`, `uniforms/`, `types/`,
  `buffer/`); their backend implementations are under `./src/d3d12` and `./src/metal`, one per
  binary. Nothing outside `src/<backend>` includes a backend header (d3d12 or metal-cpp) — what a
  caller needs of a backend it asks for with `GetNativeObject(NativeObjectType)` / `GetNativeTexture`,
  an untyped `NativeObject`, so no RHI header names a backend type.
- **Nothing here names the renderer.** `bgpu_selfcheck` compiles every public header alone, with no
  PCH, against `bgpu` only, so an `#include <bgl/...>` or `<bgl_common/...>` — or an include a header
  leaned on the PCH for — stops the build. `bgpu_check_shaders` does the same for every Slang module
  under `./shaders/src`.
- Error handling: `core::ensure` for internal problems; throw for the caller's.
- Verification: `just test bgpu` — `[compute]` is an owner with no renderer in the process — then the
  renderer's suite, `bgl_extended_tests`, which drives the same RHI harder.

## D3D12 (`./src/d3d12`)

- PCH is `./src/d3d12/pch.h`. Don't `#include` the headers in it.
- HRESULTs: `D3D12CreateDevice(...) >> d3d12ErrChecker;` — `bgpu::d3d12ErrChecker`, from
  `<bgpu/d3d12/D3d12ErrorChecker.h>`, in the PCH.
- Implementation files take a `_d3d12` suffix: `IDevice` is implemented by `Device_d3d12.cpp`.
- **A target that compiles shaders needs `dxcompiler.dll` and `dxil.dll` beside the executable.**
  Slang loads both with `GetProcAddress`, so nothing imports them and vcpkg's applocal deployment
  does not stage them. `bgl_extended`'s build stages them, with the Agility SDK's `D3D12Core.dll`,
  so a target that brings up a device depends on `bgl_extended` for the staging even when it links
  no renderer — `bgpu_tests` does — and links `bgl_d3d12_agility` for the SDK's exports.
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
  whichever one on this thread was pushed last. So anything here creating one scopes its own
  `NS::AutoreleasePool` — `CommandList::Open`..`Close`, `CommandQueue::Flush` — rather than letting it
  reach whatever net its owner holds (the renderer's is `AutoreleaseNet_metal.h`, in `bgl_extended`).
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

## Shaders (`./shaders`)

- `./shaders/src` is the RHI's Slang half — the IDL offset primitives (`idl.Entry`, `idl.Range` …),
  `idl.ErrorCode` and `idl.DebugRecord`, the buffer family (`lib.types.*Buffer`, and `lib.types.BoxedHandle` for a buffer of handles), the bindless texture helpers (`lib.types.Texture`) and the GPU assert
  channel (`lib.debug.dbg`) — staged into `./shaders/src` beside the executables by
  `bgpu_copy_shaders`, first of every copier, since every other tree imports it. An import name is the
  path under that root, so a module here keeps its name wherever it is staged from.
- Its C++ mirrors: the hand-written offset primitives under `include/bgpu/idl`, and the modules with a
  concrete layout listed in `BGPU_IDL_CPP_SOURCES`, generated per backend as `bgpu::idl`
  ([docs/idlgen.md](../../docs/idlgen.md)).
- `./shaders/tests/bgpu` is `bgpu_tests`' own kernels, staged under `./shaders/tests/bgpu` and
  imported as `bgpu.<Name>`.
