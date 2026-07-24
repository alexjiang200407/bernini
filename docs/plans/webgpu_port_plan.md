# WebGPU port — implementation plan

Adds a WebGPU backend to `bgl` with the browser as the end target: a build of the renderer that
runs from a link, no install. The current backend is D3D12/Windows; a Metal/macOS port is in
flight on the `feat/metal-*` branches and this plan deliberately rides on the seams that port has
already cut (backend gating, `PLATFORM=MACOS`, per-backend tests).

This is a *plan*, not a mirror of code. It records what the survey found, the design decisions and
their reasons, the CMake/preset changes, and the staging with a verification gate per checkpoint.
When the work lands, the durable parts move to `docs/webgpu.md` plus updates to
[rhi.md](../rhi.md), [shader_cache.md](../shader_cache.md), [passes.md](../passes.md), and
[geometry_layout.md](../geometry_layout.md).

**The load-bearing property: the public API and the scene data model do not change.** `IGraphics`
/ `IScene` / `ISceneView`, the meshlet-partitioned geometry buffers, and the GPU-driven
cull→compact→draw chain all survive. What changes is *how a meshlet becomes pixels* (WebGPU has no
mesh shaders) and *how a shader reaches a resource* (WebGPU has no bindless). Because the API is
stable, every existing golden-image test is a port regression test for free.

---

## 1. What the survey found

The engine is built on exactly the four D3D12 features browser WebGPU lacks:

* **Every raster draw is a mesh-shader dispatch; there is no vertex-draw path in the RHI.**
  `ICommandList` ([cmd/CommandList.h](../../libs/bgl/src/cmd/CommandList.h)) exposes
  `DispatchMesh` / `DispatchMeshIndirect` / compute `Dispatch` and nothing else — no
  `DrawIndexed`, no vertex/index buffer binding. `Forward_StaticMesh.slang` is a full
  amplification→mesh pipeline (`ASBase` culls meshlets and `DispatchMesh`es survivors with a
  payload); even the skybox and fullscreen rect are mesh shaders. WebGPU has neither mesh nor
  amplification shaders, and they are not on the standard's near-term roadmap.
* **Bindless is the resource model, not a feature.** Shaders reach everything through Slang
  `.Handle` types (`Texture2D.Handle`, `SamplerState.Handle`, the `types.*Buffer` smart-buffer
  structs), which lower to `ResourceDescriptorHeap[index]` under SM6.6. The CPU side writes raw
  descriptor indices into constant buffers (`DescriptorHandle` is a `uint2`;
  `Uniforms::operator=` on a handle writes an index). WebGPU has fixed
  `BindGroupLayout`s and no descriptor indexing in core.
* **Indirect execution is GPU-authored but count-1.** `DispatchMeshIndirect` is always
  `ExecuteIndirect(sig, 1, …)` — a single indirect dispatch whose args compute passes wrote
  (`CompactInstances`, `TransparentSort`). The *shape* maps to WebGPU's
  `dispatchWorkgroupsIndirect`/`drawIndirect`; the blocker is only that the consumer is a mesh
  pipeline.
* **CPU/GPU sync assumes blocking waits and persistent mapping.**
  `WaitForFenceCPUBlocking` paces `BeginFrame`, recycles capture slots, and drives the Debug-build
  GPU-assert readback every frame. Readback buffers are persistently mapped. The browser main
  thread cannot block, and WebGPU readback is `mapAsync`-only. (The split-phase
  `SubmitCapture`/`TryResolveCapture` API is already async-shaped — the blocking convenience
  wrappers are the problem, not the design.)
* **Shaders compile at runtime via a native Slang session** targeting DXIL, with a two-layer disk
  cache whose payloads (DXIL, `ID3D12PipelineLibrary`) are D3D12-private. The Slang compiler
  cannot realistically ship in a wasm bundle; browser builds need precompiled WGSL. The cache
  already stores a target-neutral `ReflectedLayout` POD — the one piece that carries over intact.
* **The shell is desktop-only.** The editor is Qt Widgets on Windows, the swapchain is DXGI from
  an `HWND` (`RenderTargetDesc::wnd`), the render loop paces on blocking `Present`, and asset I/O
  is synchronous `std::ifstream` + `std::filesystem` directory walks. None of this goes to the
  browser; a small viewer app does instead.
* **What is already portable:** all five compute kernels (cull, histogram, prefix-sum, compact,
  transparent bitonic sort) use only 32-bit `InterlockedAdd`, groupshared, and `[numthreads]` — no
  wave intrinsics, no 64-bit atomics anywhere in the shader tree. The FrameGraph derives barriers
  centrally (they become no-ops on WebGPU, which is implicit-barrier). The
  `bgl_objects`/`bgl_d3d12` split and `RENDERER_BACKEND` gating give the backend seam for free,
  and the Metal branches have already generalized it once
  (`.*/src/(d3d12|metal)/.*` filters, `bgl_metal` object lib, per-backend test wiring).

  Portable at the *algorithm* level, that is — compiling the kernels as-written with
  `slangc -target wgsl` (2026.14) surfaces two mechanical migrations WGSL forces: every
  `InterlockedAdd` on a plain `uint` field must move to Slang's typed `Atomic<T>` (WGSL has no
  free-standing atomics on ordinary memory; only `PrefixSumInstances`, which has no atomics,
  compiles today), and the IDL's `uint16_t` fields (`Meshlet`, `VertexLayout`, `ChannelSource`,
  the `PsoType` underlying type) have no WGSL representation — the IDL codegen needs a WGSL
  lowering that packs 16-bit pairs into `u32` words behind accessors, without disturbing the
  D3D12/C++ byte layout. Both fixes are shared-source, not forks (§ shaders, W2).

## 2. Native first, or browser first?

**Build against `webgpu.h` with native Dawn first, on macOS; treat the browser as a link target of
the same backend, not a second backend.** This is not a detour from the browser goal — it is the
same code:

* Dawn's native library and Emscripten's `emdawnwebgpu` port implement the **same `webgpu.h` C
  API**. The old `-sUSE_WEBGPU` Emscripten path is gone; `--use-port=emdawnwebgpu` is the
  supported route and needs no ASYNCIFY (callback style + `emscripten_set_main_loop`). One
  `bgl_webgpu` backend compiles for both.
* Native buys the whole debugging arsenal the port will need: Dawn's validation/error messages,
  Metal frame capture in Xcode underneath, `bgl_tests` under Catch2, blocking waits during
  bring-up (`wgpuInstanceWaitAny` exists natively). In the browser you get none of that while also
  fighting async, COOP/COEP, and packaging.
* Native WebGPU on macOS is itself useful: it gives macOS a working renderer through Dawn→Metal
  while the hand-written Metal backend matures, and the two ports validate each other's
  golden images.
* **The full desktop target set compiles against the native backend.** The editor talks only to
  the public bgl API and already passes its window as an opaque `void*` (`winId()`), so with the
  shared surface-descriptor change it builds against WebGPU on macOS (Qt6 runs there; only
  `windeployqt` is `if(WIN32)`-gated) — and on Windows too, where Dawn→D3D12 makes the WebGPU
  editor a validation mirror of the native D3D12 one. Tests, examples, and the baker follow the
  same per-backend CMake arms the Metal port added.
* The browser-only work — async readback, rAF pacing, asset fetch, wasm packaging — is a thin
  final layer over a proven backend, rather than a variable confounded with every renderer bug.

The failure mode this avoids is real: debugging "black screen in Chrome" where the suspect list is
{new backend, new geometry path, new shader target, wasm toolchain, async lifetime} all at once.

## 3. Design decisions

### The backend slots in as `RENDERER_BACKEND=WEBGPU`

Mirror the Metal port exactly: `libs/bgl/src/webgpu/` with `_wgpu` file suffixes, a `bgl_webgpu`
object/static lib, glob filters widened to `.*/src/(d3d12|metal|webgpu)/.*`,
`RENDERER_BACKEND_WEBGPU` compile definition, `CreateGraphics` defined inside the backend.
`webgpu.h` is the only graphics API included there.

**Dawn is consumed via `FetchContent`**, gated on `RENDERER_BACKEND=WEBGPU`, with the revision
pinned in `libs/bgl/src/webgpu/CMakeLists.txt`.

vcpkg *does* have a `dawn` port — this plan originally said it did not — and on paper it is the
better fit: it would follow the repo's manifest convention, be binary-cached across build dirs,
and cover Emscripten too (the port fetches the prebuilt `emdawnwebgpu_pkg` and wires
`--use-port=`, behind the same `dawn::webgpu_dawn` target). **It does not work.** Its
`001-fix-windows-build.patch` no longer applies to the Dawn tarball it downloads, so extraction
fails before any compile; this was verified on macOS against every version in the version
database, including the newest, so pinning cannot dodge it. Re-check the port periodically — if it
is fixed upstream, switching is a one-line change plus a manifest feature (declare a `webgpu`
feature depending on `dawn`, select it from the preset with `VCPKG_MANIFEST_FEATURES` so DX12
builds never pay for it).

The cost of FetchContent is real and worth planning around: Dawn plus its fetched dependencies is
roughly 3 GB of source **per build directory**, and the first build is minutes of compiling.
`FETCHCONTENT_SOURCE_DIR_DAWN` points several build dirs at one checkout, and CI should cache it.

### Geometry: keep meshlets, replace the mesh shader with compute expansion + vertex pulling

The meshlet data model, the culling chain, and GPU-driven indirect args all stay. Only the last
hop changes — per frame, per view:

1. **Meshlet-instance expansion (compute).** A new kernel expands each *visible* instance
   (from `scene.instanceVisibility`) into `(instanceIndex, meshletIndex)` records appended to a
   per-PSO-bucketed `meshletInstanceBuffer` via `InterlockedAdd` — this absorbs the amplification
   shader's job (per-meshlet frustum/cone culling happens here). The finalize step converts each
   bucket's record count into `DrawIndirectArgs { vertexCount = count * cMaxPrimsPerMeshlet * 3,
   instanceCount = 1, … }`.
2. **Vertex-pulling draw.** One `drawIndirect` per PSO bucket (same fixed `c_PsoCount` loop
   `ForwardPass` runs today). The vertex shader derives
   `record = vertex_index / (cMaxPrimsPerMeshlet*3)`, `prim = (vertex_index % …) / 3`,
   `corner = vertex_index % 3`, reads the meshlet, walks the *same* vertexMap→byte-decode chain
   `MSMain` walks now (`DecodeVertex` and the offset-indirection chain in
   [geometry_layout.md](../geometry_layout.md) are stage-agnostic), and emits degenerate
   triangles (`NaN` position) for `prim >= meshlet.triangleCount`. Fragment shaders port with
   minor interpolant plumbing.

This preserves the invariant that expansion happens at the instance level and the geometry buffers
are untouched. The padding cost (`124`-prim meshlets are mostly full for cooked assets) is the
accepted price; a compacted-index-buffer variant (compute writes a real index buffer, then
`drawIndexedIndirect`) is the optimization if profiling demands it — the buffer contract would not
change. The skybox/fullscreen mesh shaders become trivial 3-vertex `draw` calls.

The RHI grows `Draw`/`DrawIndirect` plus a `RenderPipeline` (vertex+pixel) alongside
`MeshletPipeline`; on the WebGPU backend `MeshletKernel` creation is refused
(`GraphicsError`) — passes select the path per backend capability, they do not emulate mesh
shaders behind the old entry point. That keeps the emulation visible in pass code
(`ForwardPass` gains a second attach path) instead of hidden in the backend.

### Binding: descriptor indices become bind groups + array layers

The seam is already narrow: shaders reach resources only through the `types.*Buffer` /
`*.Handle` IDL primitives, and the CPU writes only through `Uniforms`/`ReflectedLayout`. The port
specializes both per target rather than touching every shader:

* **Buffers.** Slang's WGSL target lowers `StructuredBuffer` members to storage-buffer bindings.
  The smart-buffer structs get a WGSL-target variant where the `.Handle` member is replaced by an
  actual buffer binding; Slang reflection then reports real `(group, binding)` slots, which
  `ReflectedLayout` carries instead of D3D12 root-param/register data. `Uniforms` assignment of a
  `BufferHandle` records a bind-group entry instead of writing an index. The explicit variant is
  mandatory, not a nicety: left to itself, slangc (verified on the pinned 2026.7.1) lowers a
  `.Handle` field to an emulated descriptor heap — `@group(1) var _slang_resource_heap_0 :
  array<array<u32>>`, a runtime-sized *binding array* that core browser WGSL does not have. A
  kernel can therefore "compile to WGSL" and still be unloadable in a browser, which is why every
  cooked program must pass a real WGSL validator (Tint/naga), not just slangc. The ~12 distinct buffers
  a forward draw touches fit comfortably in WebGPU's per-stage storage-buffer limits
  (Tier-1 limit is 8 storage buffers per stage — the forward data may need packing into fewer,
  wider buffers or a second bind group; audit in W1).
* **Textures.** Arbitrary per-material texture indexing cannot survive (browser limit: 16 sampled
  textures per stage, no indexing). The replacement keeps the *index* model: all material textures
  of a format class live as **layers of shared `texture_2d_array`s** (one per format/usage class:
  sRGB base color, linear normal/ORM, …), and the material record's `DescriptorHandle` becomes an
  array-layer index. The KTX2/asset-standards work already normalizes formats and mip chains;
  the cook gains a per-class max-dimension so layers agree in size. Cube maps (IBL, skybox) are
  few and bind as ordinary named bindings. Samplers collapse to the two `StandardSampler` presets,
  bound statically.

### Shaders: cooked WGSL, same cache shape

Slang targets WGSL, but a wasm-hosted compiler is a non-starter, so the compile moves offline:

* The program cache generalizes from "D3D12's private `.bsc`" to a **target-tagged cooked
  program**: WGSL text (or a Tint-validated blob) + `ReflectedLayout` + bind-group layout, keyed
  by the same content salt. A host-side cook step (a mode of the existing shader tooling, run at
  build time like `compile_shader` validation is today) emits the full PSO permutation set —
  enumerable, since `c_Psos` is a static table.
* The **native** WebGPU backend keeps the runtime Slang session (fast iteration on macOS, same as
  D3D12 today) and treats the cooked set as a warm cache; the **browser** build ships only the
  cooked blobs and never links Slang — which also removes `slang::slang` from the wasm link line
  (it currently hangs off `bgl_objects`; the reflection walk moves behind the cook/backend seam).
* Expect Slang→WGSL back-end bugs of the same species the Metal port hit (per-stage compilation,
  semantic naming); the mitigation is the same: a build-time `compile_shader(... TARGET wgsl)`
  validation entry per pipeline so breakage is a CI failure, not a runtime surprise.
* There is no driver-PSO cache analog; WebGPU pipeline creation goes through
  `createRenderPipelineAsync` at startup and the browser caches internally.

### Sync: fence values stay, blocking becomes a capability

The fence-value vocabulary survives: the backend counts submissions and resolves completion via
`onSubmittedWorkDone` callbacks into an atomic — `PollCurrentFenceValue`/`IsFenceComplete` work
unchanged. `WaitForFenceCPUBlocking` works natively (Dawn futures/`WaitAny`) and **traps in the
browser build**; every browser-reachable path must use the non-blocking form:

* Frame pacing moves to rAF (`emscripten_set_main_loop`) + an in-flight-frame counter; the
  blocking `Present` pacing is a desktop detail the browser loop replaces.
* Captures already have the split-phase API; `ScreenshotToMemory` (blocking convenience) is
  desktop-only. GPU-assert readback becomes ring-buffered polling (it already tolerates
  several frames of latency) with `mapAsync`.
* `Flush`/`WaitIdle` (resize, teardown) are rare and become async-completed on web (resize defers
  buffer destruction behind the fence exactly as deferred destruction already does).

Threading: the first browser build is **single-threaded** — bgl on the main thread under rAF. The
editor's render-thread marshaling is desktop code. Wasm pthreads (needs COOP/COEP headers) is a
later option, not a dependency.

### Barriers: derived, then dropped

WebGPU synchronizes implicitly. The FrameGraph keeps its declared-access model (it also drives
culling and ordering); the WebGPU backend's `ICommandList::Barrier` is a no-op, and pass-boundary
transitions vanish. One real consequence: WebGPU inserts what D3D12's intra-pass UAV-barrier
exception handles manually (histogram→scan), because those become separate `dispatch` calls in one
compute pass encoder — WebGPU guarantees ordering between dispatches writing the same buffer.
No pass-code change needed.

### Shell: a viewer example, not the editor

The browser deliverable is `examples/web_viewer` (or an `apps/viewer` if it grows): loads a cooked
scene, orbits a camera. Windowing on native macOS uses SDL3 (already in the manifest, supports
both Metal-layer surfaces and Emscripten/canvas); `RenderTargetDesc::wnd` generalizes from `HWND`
to a per-platform surface descriptor (coordinate with the Metal branch, which needs the same
change — land it once). Assets ship via `--preload-file` first; streaming `fetch` is an
optimization. Qt/editor stays desktop-only, unchanged.

## 4. Current blockers (state of the world, July 2026)

Engine-side (all addressed by §3):

1. Mesh/amplification-only geometry path — no vertex draw in the RHI.
2. Bindless descriptor-index binding throughout shaders, `Uniforms`, and the resource manager.
3. Blocking fence waits + persistently-mapped readback on browser-reachable paths.
4. Runtime Slang→DXIL compilation; Slang cannot run in the wasm bundle.
5. `bgl` is a SHARED library — Emscripten dynamic linking is painful; the web build makes it
   STATIC (a CMake branch, since the intrusive-refcount ABI seam is irrelevant in a monolith).
6. Qt/HWND/DXGI shell, blocking-present pacing, synchronous `std::filesystem` asset I/O.
7. Exceptions are load-bearing (`ApiError` et al.) — supported under Emscripten
   (`-fwasm-exceptions`), at some size/perf cost; acceptable, not a rewrite.

Ecosystem-side (constraints to design around, not wait out):

1. **No mesh shaders in WebGPU** and none coming soon; Slang's WGSL target accordingly has no
   mesh-stage support. The compute-expansion path is a permanent design, not a shim.
2. **No bindless in core WebGPU**; binding_array-style extensions exist only outside the browser.
3. **Browser limits**: ~16 sampled textures / 8 storage buffers per stage at base limits, no
   persistent mapping, single queue, `mapAsync`-only readback, no multi-draw-indirect (count-1
   indirect is fine — matches current usage).
4. **Toolchain**: `-sUSE_WEBGPU` is removed; use `--use-port=emdawnwebgpu` (Emscripten ≥ 4.0.10),
   callback-based API, no ASYNCIFY. vcpkg's `wasm32-emscripten` community triplet covers the
   portable deps (glm, stb, meshoptimizer, ktx, tinygltf, nlohmann-json, sdl3); `shader-slang`,
   `cpptrace`, `catch2`, and the DirectX packages drop out of the web dependency set.
5. **Wasm threads need cross-origin-isolated hosting** (COOP/COEP) — avoided entirely by the
   single-threaded first build; relevant only if a render worker lands later.
6. Safari/Firefox WebGPU support has shipped but limits and WGSL corner-cases vary; Chrome is the
   bring-up target, others are a compatibility pass.
7. **Multi-draw indirect is not in the standard yet.** Chrome ships
   `chromium-experimental-multi-draw-indirect` behind the unsafe-WebGPU flag, Dawn/wgpu support it
   natively, and it is the canonical GPU-driven-rendering ask — but the spec (CR as of
   March 2026) has not adopted it. Do not depend on it; do stay upgrade-ready (see the roadmap
   audit below — `compactDispatchArgs` is already the contiguous args array MDI consumes).

### Roadmap compatibility audit

Against [ROADMAP.md](../../ROADMAP.md) (which already lists WebGPU as an RHI target), the planned
features sort into three bins:

**Clean on WebGPU** — the bulk. All crowd-sim/animation compute (spatial grid, prefix sums, flow
fields, VAT sampling — the roadmap already specifies unorm-packed VAT, sidestepping the
`float32-filterable` optional feature), fixed-point 32-bit `atomicAdd` damage accumulation,
CSM/depth-prepass (depth textures and comparison samplers exist), TAA/motion vectors, WBOIT
(MRT + blend), texture atlasing and "kit index into a texture array" (arrays are core WebGPU —
the crowd-variation design happens to be exactly the §3 binding model), LOD compaction into
per-tier indirect args (count-1 per bucket; bucket counts are static).

**Needs a per-backend seam** —
* *Wave intrinsics are planned, explicitly*: notify detection ("wave-ballot + one `atomicAdd` per
  wave"), wave-aggregated event append, and the shared-source kernel harness notes. WGSL has a
  standardized `subgroups` optional feature (shipped in Chrome) covering ballot/reductions, but
  it is optional and subgroup size varies — every wave-optimized kernel needs the plain-atomics
  fallback the harness already anticipates.
* *HZB via FidelityFX SPD*: the single-dispatch trick uses a global atomic + subgroup ops —
  portable, but it is an HLSL port with the same subgroups caveat; `GatherRed` maps to
  `textureGather`.
* *"Readback ring, persistently mapped"*: no persistent mapping on WebGPU — same ring, `mapAsync`
  per slot (§3 sync design).
* *Every new raster pass pays the divergence tax*: terrain, foliage, particles-as-geometry,
  decals, outline — each is authored once for the mesh-shader path and once for the
  vertex-pulling path. Pixel shaders and the vertex-decode chain are shared Slang; the per-stage
  entry points are the duplicated part. This is the largest *ongoing* cost of the port, and the
  reason the vertex-pulling path must be a first-class peer, not a shim.

**Desktop-only, degrade gracefully** — async compute (WebGPU has one queue; the scheduling win
evaporates, correctness is unaffected), GPU `printf` / GBV / DRED / Aftermath (native tooling;
the buffer-based GPU-assert path ports, its readback goes async), GPU timestamps (optional
feature, quantized in browsers), determinism-diffing as a race detector (run natively —
cross-vendor float determinism in the browser is weaker), GPU virtual memory (no sparse
resources; it is on the optional list anyway).

Sources: [Emscripten WebGPU docs](https://emscripten.org/docs/porting/multimedia_and_graphics/WebGPU-support.html),
[emdawnwebgpu](https://github.com/google/dawn/blob/main/src/emdawnwebgpu/pkg/README.md),
[USE_WEBGPU deprecation](https://github.com/emscripten-core/emscripten/pull/24220),
[Slang target support](https://github.com/shader-slang/slang/blob/master/docs/target-compatibility.md),
[WebGPU mesh-shader status](https://github.com/gpuweb/gpuweb/issues/3015).

## 5. CMake preset changes

The Metal branch already contributes the `macos` hidden preset (`PLATFORM=MACOS`, Darwin
condition); land on top of it (or copy it if this races the Metal merge). Additions:

```jsonc
// hidden building blocks
{
  "name": "webgpu",
  "hidden": true,
  "cacheVariables": { "RENDERER_BACKEND": "WEBGPU" }
},
{
  "name": "emscripten",
  "hidden": true,
  "generator": "Ninja",
  "toolchainFile": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
  "cacheVariables": {
    "CMAKE_MAKE_PROGRAM": "ninja",
    "VCPKG_TARGET_TRIPLET": "wasm32-emscripten",
    "VCPKG_CHAINLOAD_TOOLCHAIN_FILE":
      "$env{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake",
    "PLATFORM": "WEB"
  }
},

// concrete presets
{
  "name": "macos-clang-webgpu-debug",
  "displayName": "macOS Clang Debug ninja - WebGPU (Dawn native)",
  "binaryDir": "${sourceDir}/build/macos-webgpu-debug",
  "inherits": [ "vcpkg", "macos", "clang", "webgpu", "debug" ],
  "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
},
{
  "name": "macos-clang-webgpu-release",
  "displayName": "macOS Clang Release ninja - WebGPU (Dawn native)",
  "binaryDir": "${sourceDir}/build/macos-webgpu-release",
  "inherits": [ "vcpkg", "macos", "clang", "webgpu", "release" ],
  "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
},
{
  "name": "web-emscripten-webgpu-debug",
  "displayName": "Web Emscripten Debug - WebGPU (browser)",
  "binaryDir": "${sourceDir}/build/web-webgpu-debug",
  "inherits": [ "emscripten", "webgpu", "debug" ],
  "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "BUILD_TESTS": "OFF" }
},
{
  "name": "web-emscripten-webgpu-release",
  "displayName": "Web Emscripten Release - WebGPU (browser)",
  "binaryDir": "${sourceDir}/build/web-webgpu-release",
  "inherits": [ "emscripten", "webgpu", "release" ],
  "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
}
```

Matching `buildPresets` entries follow the existing pattern. Notes:

* The `web-*` presets override `debug`'s `BUILD_TESTS=ON` — Catch2/`bgl_tests` run under the
  **native** WebGPU preset; there is no browser test runner in this plan.
* `PLATFORM` gains a `WEB` value: `libs/core` currently fatals on anything but `WINDOWS`/`MACOS`.
  The posix layer turned out **not** to be reusable — `src/posix/` is a macOS layer by another
  name (`_NSGetExecutablePath`, `dladdr`) — so web gets its own `src/web/`, where both entry
  points degrade to constants: one wasm module has no library to locate and no executable path.
* CMake-side, `libs/bgl/CMakeLists.txt` gains the `WEBGPU` arm of every `RENDERER_BACKEND`
  branch (subdirectory, object embedding, compile definition, per-backend tests), the glob
  filters widen to `(d3d12|metal|webgpu)`, and `bgl` becomes `STATIC` when `EMSCRIPTEN` is set.
  `find_package(Dawn)` lives in the `src/webgpu/CMakeLists.txt` arm and covers both targets.
* Emscripten's toolchain must be chainloaded *through* vcpkg (as shown) so manifest dependencies
  resolve for `wasm32-emscripten`; `EMSDK` must be exported — `just init` records it in
  `scripts/config.json` like the other machine paths. `scripts/build.py` needs no change:
  `cmake_tools.needs_msvc_env` already returns false off Windows.

## 6. Staging and verification

Each checkpoint gates on something executable. The golden-image suite is the backbone: it is
backend-independent by construction (same API, same assets, same camera), so "goldens match" is
the port's definition of done at every raster stage. Expect per-backend tolerance thresholds
(rasterization differences), following the Metal port's precedent.

* **W0 — scaffolding.** Presets, `PLATFORM=WEB` in core, `src/webgpu` skeleton with
  device/instance/adapter creation, Dawn FetchContent, backend-gated CMake arms.
  *Gate:* `just build --preset macos-clang-webgpu-debug` succeeds; a smoke test creates a device
  and reports adapter info; Windows DX12 CI stays green (the seam must not disturb it).
* **W1 — compute RHI on native Dawn.** Buffers, resource manager (slot table → `WGPUBuffer`),
  uniforms→bind-group emission, compute pipelines from hand-written WGSL fixtures, fence
  emulation, readback. Storage-buffer-per-stage limit audit happens here.
  *Gate:* the compute-tagged subset of `bgl_tests` (the Metal port's tag mechanism) passes under
  the native WebGPU preset — exact survivor counts from the readback tests, not just "runs".
* **W1.5 — first pixels: a triangle, below the public API.** A hand-written WGSL vertex+fragment
  pipeline, a render pass, and a draw of three vertices — reached through `bgl_webgpu`'s own
  objects, **not** through `CreateGraphics`. Two forms sharing one code path: an **offscreen**
  one that renders to a `WGPUTexture` and reads the pixels back (this is the CI-testable form —
  assert the centre pixel is the triangle's colour and a corner is the clear colour), and a
  **windowed** one that configures a `WGPUSurface` from the SDL3 window and presents, which is
  the visible artifact.
  *Gate:* the offscreen form asserts pixel values in `bgl_webgpu_tests`, so it runs in CI on the
  macOS runner; the windowed form is a manual check.

  This is deliberately out of dependency order, and it is worth it. It retires the two risks the
  rest of the plan never touches until late — that a `WGPUSurface` can be built from this
  engine's SDL3 window and presented, and that a render pipeline plus render pass work at all —
  while they are still cheap to debug in isolation. It needs no Slang, no bindless, no
  meshlet-expansion, no resource manager, and no FrameGraph. **Nothing built here is
  throwaway**: the surface handling becomes `IRenderTarget` and the render-pass recording becomes
  `ICommandList`'s raster half.

  **Why it cannot go through `IGraphics`, and what that costs.** `CreateGraphics` constructs a
  `RenderContext`, whose constructor eagerly initializes every pass it will ever run —
  `ForwardPass::Init` loops over `c_PsoCount` building a *meshlet* kernel per PSO, and
  `SkyboxPass::Init` builds one more
  ([RenderContext.cpp](../../libs/bgl/src/gfx/RenderContext.cpp), `ForwardPass::Init`). On WebGPU
  those calls must fail, because there are no mesh shaders. So the public API is **all-or-nothing**:
  there is no configuration in which a `CreateGraphics` call succeeds but draws nothing, and not
  even a window that merely clears is reachable before the mesh-shader replacement (W3) and the
  WGSL pipeline (W2) both exist. Getting a triangle on screen *through the engine's own API* is
  therefore a W3 deliverable, not an early one — and if that ordering ever needs to change, the
  change is to make pass initialization lazy or capability-selected rather than eager, which is a
  `RenderContext` change and should be costed as one.
* **W2 — shader pipeline.** Slang session targeting WGSL in the native backend;
  `ReflectedLayout` extended with `(group, binding)`; WGSL-target variants of the `types.*Buffer`
  primitives; the two migrations the slangc probe proved necessary — `InterlockedAdd` on plain
  fields → typed `Atomic<T>` (shared-source; D3D12 is unaffected), and a WGSL lowering in the IDL
  codegen for the `uint16_t` fields (pack pairs into `u32` behind accessors, C++/DXIL byte layout
  untouched); `compile_shader(... TARGET wgsl)` validation entries for every compute kernel.
  *Gate:* all five culling/sort kernels compile from their real Slang sources **and validate
  under Tint** (slangc acceptance alone is not the bar — see § binding), and the W1 tests pass
  against them (byte-identical results vs. the WGSL fixtures).
* **W3 — raster path, and the first pixels through the public API.** Textures/RTV/DSV (depth is
  `depth32float` — the D24S8 remap decision is already made for Metal), render pipelines, the
  meshlet-expansion kernel + vertex-pulling forward path, `IRenderTarget` over the W1.5 surface
  code, skybox as a plain draw. This is where `CreateGraphics` first returns successfully, for
  the reason given under W1.5, and therefore where `examples/bgl_window` first runs on this
  backend.
  *Gate:* golden-image tests pass under the native WebGPU preset; a cube/sphere/textured-mesh
  scene is pixel-compared against the D3D12 goldens within tolerance. Frustum culling goldens
  prove the indirect chain (culling is image-invariant, so goldens are the gate — the same
  property [passes.md](../passes.md)'s compaction pipeline is tested by today).
* **W4 — materials.** Texture-array material atlas + cook-side size classing, IBL bindings,
  transparent path (sort compute is already ported; the three partition draws become
  `drawIndirect`).
  *Gate:* full PBR + transparency goldens; the asset baker round-trips the sample assets into
  array layers with no visual diff on D3D12 (the array model must not regress the native path).
* **W5 — browser build.** `web-emscripten-webgpu-*` presets link `emdawnwebgpu`; cooked-WGSL-only
  shader load (no Slang in wasm); rAF main loop; blocking paths trapped; `--preload-file` assets;
  `examples/web_viewer`.
  *Gate:* the viewer renders the W4 scene in Chrome from a local static server; a headless-Chrome
  screenshot in CI diffs against the golden within tolerance — this closes the loop on "the
  intended target is a link".
* **W6 — hardening.** Async captures/GPU-asserts in the browser, Safari/Firefox compatibility
  pass, wasm size budget (exceptions, Release LTO), fetch-based asset streaming, perf profile of
  the vertex-pulling path (decide whether the compacted-index-buffer variant is warranted).
  *Gate:* viewer runs in Safari; bundle size and first-frame time recorded against a budget.

Landing order note: W0–W2 are independent of the Metal branches except for the shared
`macos` preset and surface-descriptor change; coordinate those two pieces so neither port rebases
the other's seam.
