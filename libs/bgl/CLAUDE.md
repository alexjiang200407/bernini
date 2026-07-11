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
- To handle d3d12 HRESULT error returns `D3D12CreateDevice(...) >> d3d12ErrChecker;` d3d12ErrChecker located in libs/bgl/src/d3d12/ErrorChecker.h and is part of the PCH.h so don't `#include` it.
- Doesn't have an include directory, all headers are included.
- Implementation files (.h and .cpp) should have a _d3d12 suffix.
    e.g. We have IDevice class for API agnostic device, the Device_d3d12.cpp will be the class representing the d3d12 device class.
- CMake: `./src/d3d12/CMakeLists.txt`
- Verification: Check logs, bgl_tests

## bgl_tests

- After running bgl_tests always check the log to see the warnings, errors and basic info.

## Shaders

- Shaders are compiled at runtime. `IShader`/`CreateShader(module, entry)` only names a Slang
  module + entry point; the actual DXIL is generated per-PSO in `pipeline_util::BuildPipelineLayout`,
  which links all of a PSO's entry points into one program and pulls both the bytecode
  (`getEntryPointCode`) and the reflection/root-signature from that single linked program. Because
  both come from the same link, bindings always agree — shaders do **not** need explicit
  `register(bN, spaceM)` on their constant buffers.
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