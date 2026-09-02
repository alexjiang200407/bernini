# bgl_extended

bgl_extended is the extended-tier renderer: the implementation of the `bgl` contract for devices that
offer bindless resource access and a mesh stage. It provides higher level abstractions of Mesh,
Light and Material while hiding the graphics api. The contract it implements lives in `libs/bgl`
and is a target of its own; nothing here is part of it.

- CMake target: bgl_extended
- It is compiled to a Dynamic Linked Library.
- bgl_extended has its custom Render Hardware Interface (RHI). The interfaces are located `./libs/bgl_extended/src` but we define the polymorphic implementation elsewhere — `bgl_d3d12` or `bgl_metal`, one per binary. Do not #include a backend's headers (d3d12 or metal-cpp) for any of the sources here.
- Put all plain old data inside `./libs/bgl_extended/src/types`
- PCH is `./libs/bgl_extended/src/pch.h`. Don't `#include` the headers in here.
- Error Handling: For internal problems, use gassert. For caller (code that links to bgl_extended) problems, throw an exception so the caller can handle them
- CMake: `./CMakeLists.txt`
- Verification: Check logs, bgl_extended_tests


# Subsystems

## bgl_d3d12

- Static RHI implementation library that is linked with d3d12 runtime. All code that use d3d12 API must be located in this subsystem
- PCH is `./libs/bgl_extended/src/d3d12/pch.h` Don't `#include` the headers in here.
- To handle d3d12 HRESULT error returns `D3D12CreateDevice(...) >> d3d12ErrChecker;` d3d12ErrChecker located in libs/bgl_extended/src/d3d12/D3d12ErrorChecker.h and is part of the PCH.h so don't `#include` it.
- Doesn't have an include directory, all headers are included.
- Implementation files (.h and .cpp) should have a _d3d12 suffix.
    e.g. We have IDevice class for API agnostic device, the Device_d3d12.cpp will be the class representing the d3d12 device class.
- CMake: `./src/d3d12/CMakeLists.txt`
- Verification: Check logs, bgl_extended_tests
- **On Windows a target that compiles shaders needs `dxcompiler.dll` and `dxil.dll` beside the
  executable.** Slang loads both with `GetProcAddress` to emit and sign DXIL, so nothing imports them
  and vcpkg's applocal deployment does not stage them. `bgl_extended` copies them from the `directx-dxc` port
  (`./CMakeLists.txt`), so a target that brings up a device must depend on `bgl_extended` even when it links
  only the backend's objects — `bgl_extended_tests` does.

## bgl_metal

- Static RHI implementation library linked against Metal. All code that uses the Metal API must be
  located in this subsystem. Selected by `RENDERER_BACKEND=METAL`, and exactly one backend is built
  per binary.
- PCH is `./libs/bgl_extended/src/metal/pch.h`. Don't `#include` the headers in here.
- Implementation files (.h and .cpp) take a `_metal` suffix, as the d3d12 ones take `_d3d12`.
- Doesn't have an include directory; all headers are included.
- Metal is reached through **metal-cpp**, Apple's header-only C++ interface. It is not on vcpkg, so
  `./src/metal/CMakeLists.txt` pulls it with `FetchContent` at a pinned commit.
- `MetalImpl.cpp` is the one TU that defines metal-cpp's `*_PRIVATE_IMPLEMENTATION` macros, and so
  compiles with `SKIP_PRECOMPILE_HEADERS` — a PCH would include the headers first and the symbol
  definitions would be skipped.
- Error handling: Metal signals failure by returning nil and fills its `NSError` only *sometimes*,
  so a call can fail with no diagnosis. `MetalErrorChecker` (in the PCH, don't `#include` it) holds
  the error so a call site reads like its D3D12 counterpart:
  `library.get() >> errChecker;`. Where a call takes no error out-param — most of them — a `gassert`
  on the returned pointer is the whole check.
- **Shaders are compiled at runtime** from the staged Slang sources, to MSL via
  `newLibraryWithSource`. There is no build-time shader step on this backend: `./CMakeLists.txt`
  adds the `shaders` subdirectory only under `DX12`, so a shader error surfaces when the pass that
  needs it is first built rather than at compile time. See
  [Slang Shaders](../../docs/slang_shaders.md).
- **A scope that creates an autoreleased Metal object owns the pool it drains into.** Most Metal
  factories autorelease — `commandBuffer()`, `nextDrawable()` — and the pool the object lands in is
  whichever one on this thread was pushed last. `Graphics` holds one for its whole lifetime as a net
  for strays, and that net drains *before* the device, because a Metal object outliving the device it
  references deallocs into a purged one and segfaults. Anything creating an autoreleased object
  therefore scopes its own `NS::AutoreleasePool` — `CommandList::Open`..`Close`,
  `RenderTarget::PresentToLayer`, `CommandQueue::Flush` — rather than letting it reach that net.
  Committing a command buffer before the pool drains is safe: the driver holds its own reference
  until the buffer retires.
- **A flush is not done when its fence is.** An event signalled with `encodeSignalEvent` fires as
  the GPU passes it, and the driver goes on retiring the command buffer and releasing what it held
  for a while after — measurably, most flushes. Anything that frees a resource because "the GPU is
  idle" needs the buffer *retired*, so `CommandQueue::Flush` ends on `waitUntilCompleted`, which
  submission order extends to everything committed before it. A deferred free needs no such thing:
  its gate only drops our reference, and the in-flight buffer still holds its own.
- GPU validation comes from the environment, not a flag — see `bgl_extended_tests` below.
- CMake: `./src/metal/CMakeLists.txt`
- Verification: Check logs, bgl_extended_tests

## bgl_extended_tests

- After running bgl_extended_tests always check the log to see the warnings, errors and basic info.
- The suite is slow: nearly all of its runtime is `CreateGraphics`, which every test does at least
  once (and Catch2 re-runs a `TEST_CASE` body per `SECTION`, so a multi-section test pays it again
  each time). Budget minutes, not seconds, and do not mistake that for a hang.
- **On Metal, GPU validation comes from the environment**, and instruments every shader the way
  D3D12's GBV does:

  ```bash
  METAL_DEVICE_WRAPPER_TYPE=1 just run bgl_extended_tests                        # API validation
  METAL_DEVICE_WRAPPER_TYPE=1 MTL_SHADER_VALIDATION=1 just run bgl_extended_tests  # + GPU validation
  ```

  `--gpu-validation` does nothing here — it is read only by `Graphics_d3d12`. The shader cache's
  driver-pipeline layer is dropped automatically for such a run (see
  [Shader Cache](../../docs/shader_cache.md)); the `ShaderCache_test` case skips itself.

- **D3D12 GPU-based validation is opt-in**, via `--gpu-validation`:

  ```bash
  just run bgl_extended_tests                       # ~5 min: debug layer on, GPU validation off
  just run bgl_extended_tests -- --gpu-validation   # ~10 min: for a final verification run
  ```

  It patches every shader, which takes device creation from ~3s to ~18s and doubles the suite. The
  D3D12 **debug layer is a separate thing and stays on either way** — it is what catches ordinary API
  misuse; this only adds the shader-level checks. Run it before merging anything that touches
  shaders, barriers, or descriptors.

## Shaders

- Shaders are compiled at runtime. `IShader`/`CreateShader(module, entry)` only names a Slang
  module + entry point; the DXIL and reflection are generated per-PSO in
  `pipeline_util::BuildPipelineLayout`, which links all of a PSO's entry points into one program.
  Because bytecode and reflection come from the same link, bindings always agree — shaders do
  **not** need explicit `register(bN, spaceM)` on their constant buffers.
- A persistent shader cache (`GraphicsOptions::shaderCacheDir`) short-circuits compilation across
  runs. See [Shader Cache](../../docs/shader_cache.md) for the two-layer design, lazy module
  loading, invalidation, and why precompiled `.slang-module` IR is not used.
- Slang sessions are per thread (`src/slang/SlangSessions.h`): a thread's first compile creates
  its own global session and session, and `CreateGraphics` drops them all once every renderer PSO
  is built, because each global session's core module is a few hundred megabytes resident. Nothing
  may retain a `slang::` object past pipeline construction, or the release reclaims nothing, and a
  module never crosses threads — see the same doc.
- At runtime the Slang session resolves modules from `shaders/src` (and `shaders/tests`) beside the
  executable. `shaders/src` is staged by a target `bgl_extended` itself depends on — `bgl_copy_shader_src` on
  D3D12, `bgl_metal_copy_shaders` on Metal — so anything that brings a device up has the sources,
  and a build that stages none aborts on the first program-cache miss with "cannot open file".
  `shaders/tests` is the suite's own (`bgl_copy_shader_tests` / `bgl_metal_copy_test_shaders`). A new
  `.slang` placed under `libs/bgl_extended/shaders/src` is therefore usable at runtime by its module name
  without any CMake change.
- The `compile_shader(...)` entries in `libs/bgl_extended/shaders/CMakeLists.txt` are now **build-time
  validation only** — they invoke `slangc` per entry point to fail the build on shader errors early;
  the resulting `.dxil` files are not loaded at runtime. Add an entry when you want that validation:

```
compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/path/to/file.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "ms_6_6"
    INCLUDES     ${SLANG_SOURCE_ROOT}
    ENTRY_POINTS "MSMain"
)
```

- slang shaders can be formatted using clang-format