# bgl

bgl is the renderer, for devices that offer bindless resource access and a mesh stage. It
provides higher level abstractions of Mesh, Light and Material while hiding the graphics api.
`./include/bgl` is its public surface — what a game compiles against, [docs/bgl_api.md](../../docs/bgl_api.md)
— and `./src` the implementation behind it, which no client sees.

- CMake target: bgl, built as `BERNINI_RENDERER_LIBRARY_TYPE` says (a DLL in an editor build).
  `bgl_headers` is the public surface's usage requirements alone, for what compiles against the
  headers without linking the renderer: the backends' object libraries, `bgl_selfcheck`, an SDK
  gamelib.
- A public header never includes anything under `./src`; `bgl_selfcheck` compiles every one against
  `bgl_headers` alone. The shaders split the same way: `./shaders/include` is the contract a game's
  surface conforms to, checked alone by `bgl_check_shaders`, and `./shaders/src` the renderer's.
- bgl is built on `bgpu`'s Render Hardware Interface (RHI), `<bgpu/...>` in namespace `bgpu` — its interfaces, its backends and their rules are [libs/bgpu/CLAUDE.md](../bgpu/CLAUDE.md). What is left per backend here is the renderer's own: its swapchain, in `bgl_d3d12`, `bgl_metal` or `bgl_vulkan`, one per binary. Do not #include a backend's headers (d3d12, metal-cpp or volk) for any other source here; the backend's native objects are reached through the RHI's `GetNativeObject` / `GetNativeTexture` / `ImportNativeTexture`.
- Put all plain old data inside `./libs/bgl/src/types`
- PCH is `./libs/bgl/src/pch.h`. Don't `#include` the headers in here.
- Error Handling: For internal problems, use `core::ensure`. For caller (code that links to bgl) problems, throw an exception so the caller can handle them
- CMake: `./CMakeLists.txt`
- Verification: Check logs, bgl_tests


# Subsystems

## src/swapchain

- What every backend that presents through an `ISwapchain` shares: `Graphics` (the façade
  `CreateGraphics` returns) and `RenderTarget` (the frame's attachments, and the backbuffers: a
  swapchain's images when windowed, an offscreen ring when headless). A backend implements only
  `ISwapchain` (`ISwapchain.h`) and `CreateBackendSwapchain`.
- Compiled by `bgl_d3d12` and `bgl_vulkan`, not by `bgl_objects`: Metal has a target of its own,
  under the same name.
- A ring slot (allocator, fence) is mapped to the image the swapchain handed out for it, since a
  swapchain may hold more images than the ring has slots; a frame imports the backbuffer in the layout
  `GetBackbufferLayout` reports, `kUndefined` for an image no frame has drawn yet.

## bgl_d3d12

- The renderer's D3D12 half: `DxgiSwapchain` (`Swapchain_d3d12.cpp`). The RHI's D3D12 backend is
  `bgpu`'s.
- PCH is `./libs/bgl/src/d3d12/pch.h`. Don't `#include` the headers in here.
- Implementation files (.h and .cpp) take a `_d3d12` suffix.
- CMake: `./src/d3d12/CMakeLists.txt`
- The DLLs a D3D12 device and a shader compile load by name, and the Agility SDK's exports
  (`bgpu_d3d12_agility`), are bgpu's ([libs/bgpu/CLAUDE.md](../bgpu/CLAUDE.md)).

## bgl_metal

- The renderer's Metal half: `Graphics_metal`, `RenderTarget_metal` (the `CAMetalLayer` and the
  frame's attachments) and the thread's autorelease net. The RHI's Metal backend is `bgpu`'s, and so
  are the metal-cpp, error-checking, autorelease-pool and flush rules it follows
  ([libs/bgpu/CLAUDE.md](../bgpu/CLAUDE.md)).
- PCH is `./libs/bgl/src/metal/pch.h`. Don't `#include` the headers in here.
- Implementation files (.h and .cpp) take a `_metal` suffix.
- `Graphics` holds a share of the thread's one long-lived net for stray autoreleased objects
  (`AutoreleaseNet_metal.h`: pools are a stack, so two renderers on one thread cannot each hold a
  pool for their whole lives), and that net drains *before* the device, because a Metal object
  outliving the device it references deallocs into a purged one and segfaults. Anything here
  creating an autoreleased object scopes its own pool, as the RHI does —
  `RenderTarget::PresentToLayer`.
- CMake: `./src/metal/CMakeLists.txt`
- Verification: Check logs, bgl_tests

## bgl_vulkan

- The renderer's Vulkan half, brought up on Windows: `VulkanSwapchain` (`Swapchain_vulkan.cpp`), a
  `VkSwapchainKHR` over the window's HWND whose images are imported as borrowed textures. It
  acquires with a fence and waits for it, as DXGI's present blocks for a buffer, and presents behind
  the frame's fence value through the queue's `NativeVkQueue`. A present that finds the window
  resized remakes the images at the window's size, and the target takes it on. The RHI's Vulkan
  backend is `bgpu`'s. volk is reached through `"volk_vulkan.h"`, in bgl's own copy of the pointers
  under namespace `volk` (`VolkImpl.cpp`), which `LoadVulkanFunctions` points at the device when a
  swapchain is made: bgpu's are not exported from a shared bgpu.
- Above the renderer only `gamelib` and `bgl_sphere`, the example that proves it presents, are built
  on Vulkan (the root `CMakeLists.txt`, `BERNINI_HAS_RENDERER_CONSUMERS`).
- PCH is `./libs/bgl/src/vulkan/pch.h`. Implementation files take a `_vulkan` suffix.
- CMake: `./src/vulkan/CMakeLists.txt`

## bgl_tests

- After running bgl_tests always check the log to see the warnings, errors and basic info.
- The suite is slow: nearly all of its runtime is `CreateGraphics`, which every test does at least
  once (and Catch2 re-runs a `TEST_CASE` body per `SECTION`, so a multi-section test pays it again
  each time). Budget minutes, not seconds, and do not mistake that for a hang.
- **On Metal, GPU validation comes from the environment**, and instruments every shader the way
  D3D12's GBV does:

  ```bash
  METAL_DEVICE_WRAPPER_TYPE=1 just run bgl_tests                        # API validation
  METAL_DEVICE_WRAPPER_TYPE=1 MTL_SHADER_VALIDATION=1 just run bgl_tests  # + GPU validation
  ```

  `--gpu-validation` does nothing here — it is read only by `Graphics_d3d12`. The shader cache's
  driver-pipeline layer is dropped automatically for such a run (see
  [Shader Cache](../../docs/shader_cache.md)); the `ShaderCache_test` case skips itself.

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
- The renderer's PSOs are built together, in parallel: a pass's constructor requests its always-on
  kernels from the `PipelineBatch` in the `PassInitContext` it is handed
  (`src/passes/PassInitContext.h`: the device, the batch, the resource manager and the draw-bucket
  table, borrowed for the call) and
  `RenderContext` builds the set on `core::parallel_for` before any pass reads one. The per-draw-bucket
  meshlet kernels are the exception: `RenderContext::EnsureDrawBucketPipelinesExist` builds each draw bucket in
  the first `Draw` whose view demands it (`SceneView::DemandedDrawBuckets`), so a scene pays only for
  the draw buckets it uses and an unbuilt draw bucket's kernel is skipped by `Execute` as having nothing to
  draw. A new pass follows the same shape — request in its constructor, read kernels only from
  `CheckBindings` or later — and pipeline creation stays safe from any thread.
- A persistent shader cache (`bgpu::GpuContextDesc::shaderCacheDir`) short-circuits compilation
  across runs: the programs in the GPU context's store, the driver pipelines in this renderer's
  library beside them. See [Shader Cache](../../docs/shader_cache.md) for the two-layer design, lazy module
  loading, invalidation, and why precompiled `.slang-module` IR is not used.
- Slang sessions are the GPU context's (`libs/bgpu`, [bgpu.md](../../docs/bgpu.md))
  and per thread: a thread's first compile creates its own global session and session, and they
  are dropped after every pipeline batch — the start-up build's in `CreateGraphics`, a demand
  build's in `EnsureDrawBucketPipelinesExist` — because each global session's core module is a few
  hundred megabytes resident. The drop reaches every owner of the context, so nothing, in this
  renderer or beside it, may retain a `slang::` object past pipeline construction, or the release
  reclaims nothing; and a module never crosses threads — see the same doc. `IDevice::AddSourceModule` gives every session a
  module as text under a name, shadowing a file of that name for every owner of the context and
  joining the context's source salt, which every owner's cache keys mix in; a new or changed text
  drops the live sessions, so it runs before the batch, never during. `RegisterSurfaces`
  (`src/gfx/surface_registry.h`) is its one caller: it reflects every surface in the device
  context's `clientShaderDir` first and binds them all afterwards, because a binding drops the
  sessions the next reflection would have used.
- At runtime the Slang session resolves modules from `shaders/src` (and `shaders/tests`) beside the
  executable, then from the GPU context's `clientShaderDir`, the one directory a client adds: its
  files are in the cache salt like the engine's, and a program imports its modules by name the same
  way. Four trees are staged into the engine's: the RHI's `libs/bgpu/shaders/src` (the offset
  primitives under `idl/`, `lib/types/*Buffer`, `lib/debug/`) by `bgpu_copy_shaders`, first; the
  contract `libs/bgl/shaders/include` (`bgl/`) by `bgl_copy_contract_shaders`, and this renderer's own
  (`idl/`, `lib/`, `programs/`, `luts/`) by a target `bgl` itself depends on —
  `bgl_copy_shader_src` on D3D12, `bgl_stage_shaders` on Metal and Vulkan, each ordered after the
  contract's — so anything that brings a device up has the sources,
  and a build that stages none aborts on the first program-cache miss with "cannot open file".
  `shaders/tests` is the suite's own (`bgl_copy_shader_tests` / `bgl_stage_test_shaders`). A new
  `.slang` placed under `libs/bgl/shaders/src` is therefore usable at runtime by its module name
  without any CMake change.
- The `compile_shader(...)` entries in `libs/bgl/shaders/CMakeLists.txt` are now **build-time
  validation only** — they invoke `slangc` per entry point to fail the build on shader errors early;
  the resulting `.dxil` files are not loaded at runtime. Add an entry when you want that validation:

```
compile_shader(
    FILE         "${CMAKE_CURRENT_SOURCE_DIR}/path/to/file.slang"
    OUT_DIR      "${SHADER_OUT_DIR}"
    TARGET       "dxil"
    STAGE        "ms_6_6"
    DXC          "${SLANG_DXC}"
    INCLUDES     ${SLANG_SOURCE_ROOT}
    ENTRY_POINTS "MSMain"
)
```

- slang shaders can be formatted using clang-format