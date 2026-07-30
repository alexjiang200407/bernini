# Removing the WebGPU backend

The port is retired. Its W3 branch never merged, so this plan covers only what W0–W2 landed on
master.

## Why

WebGPU cannot express the binding model the engine is built on. There is no bindless heap, no
descriptor indexing, and no mesh/amplification stage, so every slice of the port spent its budget
emulating a capability the renderer already had — and the emulation, not the target hardware, is what
started shaping the abstractions. The payoff was running the engine in a browser, which a recorded
video delivers just as well. WebGPU as a demonstrable skill belongs in a project of its own, where its
constraints are the subject rather than a tax.

`RENDERER_BACKEND` and the `bgl_objects` split stay. Vulkan and Metal are the next backends, and the
API-agnostic seam is what the port actually bought — it is the one part worth keeping with a single arm
behind it.

## What goes

| | |
|---|---|
| `libs/bgl/src/webgpu/` | the backend, 53 files, and `dawn.cmake` |
| `libs/bgl/CMakeLists.txt` | every `elseif(RENDERER_BACKEND STREQUAL "WEBGPU")` arm |
| `CMakePresets.json` | the `webgpu` fragment and its four concrete presets |
| `.github/workflows/ci.yml` | `webgpu-macos`, `webgpu-windows`, and the Dawn download caches |
| `cmake/compile_shader.cmake` | the `wgsl` target and `-DBGL_WGSL=1` |
| `libs/bgl/shaders/CMakeLists.txt` | the WGSL copies of the nine compute kernels |
| `libs/bgl/shaders/src/` | every `#if defined(BGL_WGSL)` arm, including `Forward_StaticMesh`'s vertex-pulling `VSMain` |
| `ExpandMeshlets`, `HistogramMeshlets`, `MeshletDrawArgs`, `DrawIndirectArgs` | the mesh-shader emulation; no D3D12 consumer |
| `ReflectedLayout::isResourceHandle`, `ReflectedField::group`/`binding` | reflection that exists only to build a bind group |

## What stays

- The `macos` preset, repointed at a configuration with no `RENDERER_BACKEND` —
  `libs/bgl/CMakeLists.txt` already returns early with `bgl_objects` only in that case. One macOS CI
  leg keeps `core`, `assetlib` and their suites building under a non-MSVC compiler on a non-Windows
  host, which is worth more than the backend that first needed it.
- The generator-agnostic `scripts/`, the `just` recipes, and the clang/Ninja presets.
- `assetlib`'s format work. Its choices are stated against the hardware, not against a backend.
- The group-aligned capacity fix: a padding write could run past its allocation on any backend.

## The slices, and the gate for each

Order is dictated by the build: nothing may reference a file after the slice that deletes it, and the
CI legs must stop building a preset before the preset goes.

| | Slice | Gate |
|---|---|---|
| 1 | CI legs and presets | CI is green with two jobs instead of four, and the new macOS leg builds `core_tests` + `assetlib_tests` |
| 2 | the backend and `dawn.cmake` | `windows-ninja-msvc-dx12-debug` configures and builds with no `find_package(Dawn)` in the log |
| 3 | the WGSL shader path and the `BGL_WGSL` arms | every `compile_shader` entry still produces its `.dxil`, and `bgl_tests` renders the cube golden |
| 4 | the mesh-shader emulation | `bgl_tests` green; no `.slang` names a deleted module |
| 5 | the WGSL reflection fields | `bgl_tests` green — `MixedUniform`-equivalent D3D12 coverage is what proves no uniform offset moved |

Slice 3 is the one that can break a picture, and slice 5 the one that can break it silently: both
change code the D3D12 path executes, in service of deleting code it does not.

### The hole in the verification

There is no D3D12 device on the machine doing this work. Every slice is gated locally on `core_tests`
and `assetlib_tests` plus CI's `compile (ninja-msvc)` leg, which builds but does not run the renderer.
**`just test bgl_tests` on a Windows host is owed on every slice from 3 onward, before it merges**, and
the golden-image comparison is the specific thing to look at.

## Recovering it

`origin/feature/webgpu` and the `feat/webgpu-*` slice branches are left in place until this work
merges; PRs #142 and #166 hold the W3 review history. Nothing here is reconstructed from scratch if
the decision is revisited.
