# bgl

bgl or Bernini Graphics Library is the graphics library for the game engine. It should provide higher level abstractions of Mesh, Light, Material, while hiding the graphics api.

- CMake target: bgl
- It is compiled to a Dynamic Linked Library.
- bgl has its custom Render Hardware Interface (RHI). The interfaces are located `./libs/bgl/src` but we define the polymorphic implementation elsewhere. Do not #include d3d12 headers for any of the sources here.
- Put all plain old data inside `./libs/bgl/src/types`
- PCH is `./libs/bgl/src/pch.h`. Don't `#include` the headers in here.
- Error Handling: For internal problems, use gassert. For caller (code that links to bgl) problems, throw an exception so the caller can handle them
- CMake: `./CMakeLists.txt`
- Verification: Check logs, bgl_tests


# Subsystems

## bgl_d3d12

- Static RHI implementation library that is linked with d3d12 runtime. All code that use d3d12 API must be located in this subsystem
- PCH is `./libs/bgl/src/d3d12/pch.h` Don't `#include` the headers in here.
- To handle d3d12 HRESULT error returns `D3D12CreateDevice(...) >> d3d12ErrChecker;` d3d12ErrChecker located in libs/bgl/src/d3d12/D3d12ErrorChecker.h and is part of the PCH.h so don't `#include` it.
- Doesn't have an include directory, all headers are included.
- Implementation files (.h and .cpp) should have a _d3d12 suffix.
    e.g. We have IDevice class for API agnostic device, the Device_d3d12.cpp will be the class representing the d3d12 device class.
- CMake: `./src/d3d12/CMakeLists.txt`
- Verification: Check logs, bgl_tests

## bgl_webgpu

- Backend library built on Dawn (`webgpu.h`). All code that uses the WebGPU API must be located in
  this subsystem. It does not implement the RHI interfaces yet — today it owns the WebGPU object
  stack (instance, adapter, device, queue).
- PCH is `./libs/bgl/src/webgpu/pch.h`. Don't `#include` the headers in here.
- Doesn't have an include directory, all headers are included.
- Implementation files (.h and .cpp) should have a `_wgpu` suffix, mirroring `_d3d12`.
- Dawn comes from vcpkg behind the `webgpu` manifest feature, so a DX12 build never builds it. One
  `dawn::webgpu_dawn` target serves both the native build and the Emscripten one.
- Adapter and device requests are asynchronous. Natively they are awaited with
  `wgpuInstanceWaitAny`, which a browser build cannot do — it must drive them off the event loop.
- CMake: `./src/webgpu/CMakeLists.txt`
- Verification: Check logs, bgl_webgpu_tests

## bgl_tests

- After running bgl_tests always check the log to see the warnings, errors and basic info.
- The suite is slow: nearly all of its runtime is `CreateGraphics`, which every test does at least
  once (and Catch2 re-runs a `TEST_CASE` body per `SECTION`, so a multi-section test pays it again
  each time). Budget minutes, not seconds, and do not mistake that for a hang.
- **D3D12 GPU-based validation is opt-in**, via `--gpu-validation`:

  ```bash
  just run bgl_tests                       # ~5 min: debug layer on, GPU validation off
  just run bgl_tests -- --gpu-validation   # ~10 min: for a final verification run
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
- At runtime the Slang session resolves modules from `shaders/src` (and `shaders/tests`), which are
  staged into each target's output dir by the `bgl_copy_shader_src` / `bgl_copy_shader_tests`
  targets. A new `.slang` placed under `libs/bgl/shaders/src` is therefore usable at runtime by its
  module name without any CMake change.
- The `compile_shader(...)` entries in `libs/bgl/shaders/CMakeLists.txt` are now **build-time
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