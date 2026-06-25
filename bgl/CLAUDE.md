# bgl

bgl or Bernini Graphics Library is the graphics library for the game engine. It should provide higher level abstractions of Mesh, Light, Material, while hiding the graphics api.

- CMake target: bgl
- It is compiled to a Dynamic Linked Library.
- bgl has its custom Render Hardware Interface (RHI). The interfaces are located `./bgl/src` but we define the polymorphic implementation elsewhere. Do not #include d3d12 headers for any of the sources here.
- Put all plain old data inside `./bgl/src/types`
- PCH is `./bgl/src/pch.h`. Don't `#include` the headers in here.
- Error Handling: For internal problems, use gassert. For caller (code that links to bgl) problems, throw an exception so the caller can handle them
- CMake: `./CMakeLists.txt`


# Subsystems

## bgl_d3d12

- Static RHI implementation library that is linked with d3d12 runtime. All code that use d3d12 API must be located in this subsystem
- PCH is `./bgl/src/d3d12/pch.h` Don't `#include` the headers in here.
- To handle d3d12 HRESULT error returns `D3D12CreateDevice(...) >> d3d12ErrChecker;` d3d12ErrChecker located in bgl/src/d3d12/ErrorChecker.h and is part of the PCH.h so don't `#include` it.
- Doesn't have an include directory, all headers are included.
- Implementation files (.h and .cpp) should have a _d3d12 suffix.
    e.g. We have IDevice class for API agnostic device, the Device_d3d12.cpp will be the class representing the d3d12 device class.
- CMake: `./src/d3d12/CMakeLists.txt`

## bgl_tests

- For now ask for permission to write tests.

## Shaders

- To add a new shader add this to `bgl/shaders/CMakeLists.txt`:

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

- shaders are output to the `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders`
- slang shaders can be formatted using clang-format