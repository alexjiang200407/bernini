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

This retires WebGPU **as an RHI backend**, which is the only thing it was ever tried as. A browser
target at a different level — above the RHI rather than inside it, so the engine's binding model is not
the thing being translated — is not ruled out, and stays on the roadmap under Optional Features.

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
| `libs/bgl/idl/idlgen.cpp` | the WGSL Slang session and the padding it injects into every generated struct |

### The idlgen padding, which is not a deletion

`idlgen` opens a **third Slang session on `SLANG_WGSL`** for one rule that target does not share: WGSL
alone rounds a struct's size up to its alignment. Where it wants a wider stride than the C/C++ rules
give, `ReflectStruct` appends a real `pad` member to *both* mirrors so every target agrees, and
`InjectWgslPadding` writes it into the Slang copy the shaders import.

Those bytes are in the D3D12 structs today. Removing the session shrinks four of them, and the change
cascades — `LoosePbrMaterial` embeds nine `ChannelSource`:

| struct | now | after |
|---|---|---|
| `ChannelSource` | 16 | 12 |
| `Mesh` | 80 | 72 |
| `PbrMaterial` | 64 | 52 |
| `LoosePbrMaterial` | 176 | ~136 (nine 12-byte sources, not nine 16-byte ones) |

So this task **changes the bytes the GPU reads on D3D12**, in service of deleting a backend that no
longer exists. The generated `static_assert(sizeof(...))` and `offsetof(...)` lines move with it, which
makes them a witness rather than an obstacle: they cannot disagree with the layout, so only the golden
image can catch a shader reading the old offsets. It lands last and alone for that reason.

Also going: the interior-offset check and the whole-word check, both of which exist only to keep WGSL
and C/C++ reconciled, and the WGSL paragraphs in `docs/idlgen.md`.

Not in scope: the **16-bit integer prohibition**. `docs/idlgen.md` states it as a rule for structs a
WGSL shader loads, but `idlgen` does not enforce it — it is a shader-authoring convention, and relaxing
it is a change in permission rather than a removal. Worth revisiting separately once nothing targets
WGSL.

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

| | Task | Gate |
|---|---|---|
| 1 | CI legs and presets | CI is green with two jobs instead of four, and the new macOS leg builds `core_tests` + `assetlib_tests` |
| 2 | the backend and `dawn.cmake` | `windows-ninja-msvc-dx12-debug` configures and builds with no `find_package(Dawn)` in the log |
| 3 | the WGSL shader path and the `BGL_WGSL` arms | every `compile_shader` entry still produces its `.dxil`, and `bgl_tests` renders the cube golden |
| 4 | the mesh-shader emulation | `bgl_tests` green; no `.slang` names a deleted module |
| 5 | the WGSL reflection fields | `bgl_tests` green — `MixedUniform`-equivalent D3D12 coverage is what proves no uniform offset moved |
| 6 | the idlgen WGSL session and its padding | `bgl_tests` green **and the cube golden matches** — four GPU struct strides change, so this is the one task whose gate is a picture rather than a build |

Tasks 3 and 6 can break a picture; task 5 can break one silently. All three change code the D3D12 path
executes, in service of deleting code it does not. Task 6 goes last and alone because it is the only one
that moves bytes the shaders address by offset.

### The hole in the verification

There is no D3D12 device on the machine doing this work. Every task is gated locally on `core_tests`
and `assetlib_tests` plus CI's `compile (ninja-msvc)` leg, which builds but does not run the renderer.
**`just test bgl_tests` on a Windows host is owed on every task from 3 onward, before it merges**, and
the golden-image comparison is the specific thing to look at.

Task 6 cannot be merged on a build alone. Its `static_assert`s are generated from the same layout the
shaders are, so they agree by construction and prove nothing; a shader still reading a 64-byte
`PbrMaterial` stride would compile, run, and draw the wrong thing.

## Recovering it

`origin/feature/webgpu` and the `feat/webgpu-*` slice branches are left in place until this work
merges; PRs #142 and #166 hold the W3 review history. Nothing here is reconstructed from scratch if
the decision is revisited.
