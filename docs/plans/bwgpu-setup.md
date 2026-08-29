# bwgpu-setup — implementation plan

## Context

Bernini has one renderer *architecture*, not one backend: bindless end to end, mesh-shader only,
GPU-driven. `bgl_d3d12` and `bgl_metal` both implement it, chosen at configure time by
`RENDERER_BACKEND` ([libs/bgl/CMakeLists.txt](../../libs/bgl/CMakeLists.txt) `:64-68`). The browser
clears none of that bar — no descriptor indexing, no mesh shaders, no multi-draw indirect — and
[ROADMAP.md](../../ROADMAP.md) `:379` has already ruled on where a browser target would go: *"not
an RHI backend. Tried once as one and abandoned … Anything here sits above the RHI."* The seam it
names is bgl's public interface.

That interface happens to be in good shape for it. Nothing under
[libs/bgl/include/bgl](../../libs/bgl/include/bgl) names a descriptor heap, a pipeline object or a
draw verb in a signature; the vocabulary is geometry, materials, textures, instances and cameras.
But it is neutral **by accident**, and nothing in the tree records that it must stay so. Three
things are already rotting:

- **`assetlib::Submesh`'s plain vertex/index ranges are load-bearing and unmarked.**
  `vertexByteOffset` / `vertexCount` / `indexByteOffset` / `indexCount` / `indexType`
  ([Mesh.h](../../libs/assetlib_structs/include/assetlib_structs/Mesh.h) `:31-35`) sit beside
  `firstMeshlet` / `meshletCount`, and `BMesh::indexData`
  ([BMesh.h](../../libs/assetlib_structs/include/assetlib_structs/BMesh.h) `:30`) beside the three
  meshlet arrays. Grepping `libs/bgl/src` and `libs/gamelib` for `indexData` and `indexByteOffset`
  returns **nothing** — the shipped renderer uploads `meshletTriangles` and `meshletVertices`
  instead. They read as dead weight, `sizeof(Submesh) == 96` (`Mesh.h:44`) pins the layout without
  saying why the fields exist, and the next cook-size pass deletes them. That one change is the
  difference between "a lot of work later" and "re-cook every asset in the project first".
- **`PsoType` is in the public surface and should never have been.** Its enumerators are concrete
  mesh-shader pipeline permutations — `kOpaque_StaticMesh_PBR`, `kAlphaTest_VatMesh_PBR`
  ([PsoType.h](../../libs/bgl/include/bgl/PsoType.h) `:9-27`). `IScene.h:14` includes it, **no
  public declaration uses it**, and nothing outside `libs/bgl` references it in code at all. The
  comment claiming otherwise ([libs/bgl/idl/src/CMakelists.txt](../../libs/bgl/idl/src/CMakelists.txt)
  `:51`, *"PsoType is used by ForwardPass and callers"*) is right about `ForwardPass`, which is
  internal, and wrong about the callers.
- **The public surface drifts one field at a time.** `TextureAssetHandle::srvBindlessIndex`
  ([TextureAssetHandle.h](../../libs/bgl/include/bgl/TextureAssetHandle.h) `:13`) is already there,
  documented as "the bindless index of the SRV", and nothing stops the next one.

Separately and independently of any of this, three documents are **wrong today**. They still
present D3D12 as the only backend, months after Metal shipped: [docs/rhi.md](../rhi.md) `:5` ("The
concrete backend (`bgl_d3d12`)"), [docs/bgl_api.md](../bgl_api.md) `:6` ("the `bgl_d3d12` backend is
never visible here"), [docs/slang_shaders.md](../slang_shaders.md) `:3` ("compiled to DXIL", where
Metal compiles MSL at runtime — `libs/bgl/CMakeLists.txt:125-126`), and
[libs/bgl/CLAUDE.md](../../libs/bgl/CLAUDE.md) § Subsystems documents `bgl_d3d12` and `bgl_tests`
with no `bgl_metal` entry.

So this feature lands the cheap half: the guardrail, the neutrality sweep, and the record. The
renderer waits, and this plan does not design it.

## Decisions

- **ADR-1 — the seam is bgl's public interface, not the RHI.** The only graphics pipeline object is
  `IMeshletPipeline` and the draw verbs are `Dispatch`, `DispatchMesh` and `DispatchMeshIndirect`
  ([docs/rhi.md](../rhi.md) `:136,140,185,279`), so a raster backend under that interface would
  implement pure virtuals it has no equivalent for. Worse structurally: `libs/bgl/CMakeLists.txt:7`
  excludes only `src/d3d12|metal` from `bgl_objects`, so Scene, SceneView, the FrameGraph and every
  pass are backend-common — and every geometry pass is a mesh-shader draw. A third RHI arm would
  link a common half it cannot run. *Rejected: a WebGPU RHI backend, for that reason. Rejected: a
  vertex-pipeline path inside the existing renderer, which doubles every pass and every shader in
  `bgl_objects` permanently to serve a target that also needs different culling and different
  material binding.* ROADMAP already holds this position; nothing in the tree records it as a
  decision, which is what this feature fixes.
- **ADR-2 — the contract gets its own directory and its own target.** The public headers are
  hoisted to `libs/bgl_intfc/include/bgl/`, one copy, and `libs/bgl/` becomes what it actually is —
  the mesh-shader renderer. The `bgl::` namespace and the `<bgl/...>` include spelling are unchanged
  because the `include/bgl/` suffix moves with the headers. *Rejected: an INTERFACE target pointing
  at `libs/bgl/include`, which enforces identically at build time and moves no files. Taken anyway
  so `bgl` names one renderer rather than both the contract and one of its implementations, and so
  the intent to add a second is legible in the tree rather than only in a spec.*
- **ADR-3 — enforcement is one translation unit, and it is a library.** `bgl_intfc` is an INTERFACE
  target, so on its own it compiles nothing and enforces nothing. `bgl_intfc_selfcheck` is a STATIC
  library of one TU that includes `<bgl/bgl.h>` and links only `bgl_intfc`; the umbrella header
  reaches the whole surface transitively, so one TU proves the closure. If a public header ever
  reaches into `libs/bgl/src`, that target fails to build in a PR rather than in three years.
  *Rejected: an executable. The public headers declare exported symbols only a renderer defines —
  `CreateGraphics`, `CookStaticMesh`, `PreparedStaticMesh`'s special members — and the four
  `template class BGL_API core::SharedRef<...>` instantiations resolve under `dllimport` when
  `BGL_EXPORTS` is absent. A static library is never linked into an executable, so it never demands
  them. This target guards what the headers **include**, and deliberately nothing else.*
- **ADR-4 — `PsoType` is demoted from the public surface to `bgl::idl`.** It moves from
  `IDL_PUBLIC_CPP_SOURCES` to `IDL_CPP_SOURCES`, which relocates it from the committed
  `include/bgl/` tree to the generated one and requalifies every use site. *Rejected: dropping the
  unused `#include` from `IScene.h` and leaving the header public, which is a two-line diff that
  leaves the single worst vocabulary break sitting inside the contract target ADR-2 creates.*
- **ADR-5 — `Submesh`'s dual form is recorded in `Mesh.h` itself, not only in the docs.** Both
  [docs/geometry_layout.md](../geometry_layout.md) and
  [docs/asset_standards.md](../asset_standards.md) get it, but the person about to delete those
  fields is reading the struct. *Rejected: docs-only, as the source spec proposed, which relies on
  that reader having read them first.*
- **ADR-6 — renderer selection stays configure-time, and `bwgu` would be a third `RENDERER_BACKEND`
  arm producing the same target name.** `BGL_API` is a single macro with no per-DLL name
  ([api.h](../../libs/bgl/include/bgl/api.h) `:3-11`), and the public headers carry explicit
  `template class BGL_API core::SharedRef<...>` instantiations that assume exactly one exporting
  DLL. You never ship a WebGPU renderer and a D3D12 one in the same binary. *Rejected: runtime
  backend selection (`CreateGraphics(backend, opts)`), which is what Unreal's dynamically-loaded RHI
  modules and bgfx's init-time backend pick both do — the standard, deviated from knowingly. It buys
  nothing for a target that is one renderer per binary, and costs the `BGL_API` model, the explicit
  instantiations and a single-renderer link.* Recorded because this feature re-affirms the deviation
  rather than inventing it.
- **ADR-7 — the neutrality rule is prose, enforced by review.** One short note in
  [docs/bgl_api.md](../bgl_api.md) saying the public surface stays renderer-agnostic. *Rejected: a
  banned-vocabulary test over the public headers. It would police English, so the first legitimate
  use of "descriptor" in a comment earns an allowlist entry, and the allowlist is what drifts.*

## Non-goals

Read literally by [bcp-precheck](../../.claude/agents/bcp-precheck.md) § 4 against every task's diff.

- **No `libs/bwgu`, no third `RENDERER_BACKEND` value, no WebGPU code of any kind.** Every change
  here is justified by the tree as it stands today; none presumes the renderer. A CMake arm with no
  renderer behind it configures but cannot build, so no preset and no CI job would exercise it.
- **No answer to the renderer's open questions** — material binding, culling and submission, the
  FrameGraph's reduced role, shader strategy, feature parity. They are recorded as open, not
  decided.
- **No renaming of `GraphicsOptions`' capacity fields.** `maxCbvSrvUavs`, `maxRtvs`, `maxDsvs`
  ([IGraphics.h](../../libs/bgl/include/bgl/IGraphics.h) `:70-79`) keep their names; they get one
  advisory comment. No `ResourceBudget` nested struct.
- **No change to `SceneDesc::initialMeshlets`** ([IScene.h](../../libs/bgl/include/bgl/IScene.h)
  `:40`). It is documented as a starting size, not a ceiling; it joins the same advisory note.
- **No change to `RenderTargetDesc::wnd`** ([IRenderTarget.h](../../libs/bgl/include/bgl/IRenderTarget.h)
  `:31`). It is already an opaque `void*`; only its comment enumerates HWND and CAMetalLayer, and a
  third case gets added when there is a third renderer.
- **No change to `GraphicsOptions::gpuCapturePath`** ([IGraphics.h](../../libs/bgl/include/bgl/IGraphics.h)
  `:63-64`). It is openly per-backend and says so — "Metal only" — which is the honest shape for a
  field only one renderer can serve. A renderer that cannot capture ignores it, exactly as the
  capacities note says.
- **No mechanical gate on comment wording** (ADR-7).
- **No Vulkan work.** Vulkan fits *under* the RHI as a third backend and reuses all of
  `bgl_objects`; it is a different change at a different seam, and the two cost an order of
  magnitude apart.
- **No behaviour change anywhere.** Every task is a rename, a move, a comment or a document. If a
  test's expectations change, something has gone wrong.

## Acceptance

- `bgl_intfc_selfcheck` builds, and **fails to build** when a public header is made to include
  something under `libs/bgl/src` — demonstrated deliberately once, then reverted.
- `just build && just test` green on `macos-clang-metal-debug` locally, and CI green on both
  `windows-ninja-msvc-dx12-debug` and `macos-clang-metal-debug` (`.github/workflows/ci.yml:61,136`),
  since the CMake changes affect the two arms differently.
- `just idl` after the output-directory move regenerates the public headers **byte-identically** in
  their new home — `git status` clean afterwards.
- On the hoist task, `git diff --stat` shows **no client source file changed**: CMake, `.gitattributes`,
  `scripts/gen_idl.py` and the moved headers only. That is the claim ADR-2 rests on, and it is
  checkable.
- This grep over the public headers returns **only the three sites the non-goals leave standing** —
  `IRenderTarget.h:29-30` (`wnd`'s HWND/CAMetalLayer comment), `IGraphics.h:63-64` (`gpuCapturePath`,
  "Metal only") and the `maxCbvSrvUavs` explanation under `IGraphics.h`'s advisory note — for five
  lines in total. Anything else is a missed site. `SceneDesc::initialMeshlets` is a non-goal too but
  never appears here: `\bmeshlets?\b` finds no boundary inside `initialMeshlets`, so the identifier
  is invisible to this gate and has to be watched by eye.

  ```
  grep -rn -iE "\bmeshlets?\b|\bbindless\b|\bdescriptors?\b|\bheaps?\b|\bdispatch\b|\bsrv\b|\bpso\b|\bd3d|\bmetal\b|\bvulkan\b" libs/bgl/include/bgl/
  ```

  **24 lines before task 4, 5 after.** *Corrected during task 4: this was first written with bare
  alternatives and the wrong counts.* Every term needs its word boundaries, and two of them prove
  why — unbounded, `metal` matches `metallicFactor` (`IScene.h:79,154,170`) and `heap` matches
  **`cheaply`** (`ISceneView.h:14`). A gate that can never come back clean is a gate the next person
  reads as noise and stops running.

## What the survey found

**The public surface is 23 headers and its include closure is exactly two libraries.** Every
`#include` in `libs/bgl/include/bgl/` resolves to `<core/...>`, `<assetlib_structs/...>` or another
`<bgl/...>` — no `<glm/...>` (reached through `<core/glm.h>`, and `core` links `glm::glm` PUBLIC),
no `<slang...>`, and nothing under `libs/bgl/src`. So `bgl_intfc`'s link list is `core` and
`assetlib_structs`, and that is verified rather than assumed. Both are STATIC libraries and `core`
drags `spdlog::spdlog_header_only` in PUBLIC, so `bgl_intfc` is not the zero-cost header target its
name suggests — acceptable, since every client links `core` regardless.

**No *consumer* names `libs/bgl/include`, but six things inside `libs/bgl` do.** `gamelib`,
`apps/editor` and the four examples all pick the directory up transitively through `bgl_objects`'
PUBLIC include, and no source file anywhere spells the path — that is what makes the hoist
contained. What declares it:

| Site | Spelling |
|---|---|
| `libs/bgl/CMakeLists.txt:31` (`bgl_objects`) | `${CMAKE_CURRENT_SOURCE_DIR}/include` |
| `libs/bgl/CMakeLists.txt:107` (`bgl`) | `${CMAKE_CURRENT_SOURCE_DIR}/include` |
| `libs/bgl/src/d3d12/CMakeLists.txt:29` | `${CMAKE_CURRENT_SOURCE_DIR}/../../include` |
| `libs/bgl/src/metal/CMakeLists.txt:27` | `${CMAKE_CURRENT_SOURCE_DIR}/../../include` |
| `libs/bgl/CMakeLists.txt:216` (`bgl_tests`, DX12) | `${CMAKE_CURRENT_SOURCE_DIR}/include` |
| `libs/bgl/CMakeLists.txt:297` (`bgl_tests`, Metal) | `${CMAKE_CURRENT_SOURCE_DIR}/include` |

*Corrected twice.* Task 1 added the backend pair; task 6 added the `bgl_tests` pair. The original
claim was two. **The backends spell the path relatively, so `grep -rn "libs/bgl/include"` does not
find them**, and the `bgl_tests` entries are duplicated across the two variants a hundred lines
apart. The backends become `target_link_libraries(<backend> PUBLIC bgl_intfc)` like `bgl_objects`;
the `bgl_tests` lines are simply deleted, since the headers arrive transitively through
`bgl_objects` → `bgl_intfc`.

**None of the six would have failed the build.** CMake and clang both accept a `-I` naming a
directory that does not exist, so a missed site is silent — which is why the site list, not the
compiler, is what this task rests on. Two *generators* hardcode the path as well — the next
paragraph.

**Two places hardcode the IDL public output directory, and the source spec missed both.**
`libs/bgl/idl/src/CMakelists.txt:7` sets `IDL_PUBLIC_CPP_OUT_DIR` to
`${CMAKE_SOURCE_DIR}/libs/bgl/include/bgl`, and `scripts/gen_idl.py:64` sets `PUBLIC_CPP_OUT_DIR` to
the same path independently. `just idl` and the CMake custom command are two generation paths over
one list, and the file's own comment (`:13-15`) says they must not drift. Move one without the other
and they emit into different trees.

**`PsoType`'s blast radius is 47 references across 10 files under `libs/bgl/src`** —
`types/SubmeshInstance.h`, `util/util.{h,cpp}`, `passes/ForwardPass.{h,cpp}`,
`passes/CompactInstancesPass.cpp`, `scene/{SceneView,Scene,CullState}.cpp`,
`debug/DebugReadback.h` — plus 8 files under `libs/bgl/tests/src`. The Slang side is unaffected:
`libs/bgl/shaders/src/idl/PsoType.slang` is generated from the same module and its namespace does
not change. `docs/passes.md:362` notes a `static_assert` pins `c_Psos`' order against `PsoType`, so
a bad requalification does not compile.

**`srvBindlessIndex` has exactly one reader**, `libs/bgl/src/uniforms/Uniforms.h:258`, which wraps
it in a `DescriptorHandle`. The source spec said no client reads it, which is true of `gamelib` and
`apps/editor` but not of bgl itself.

**The vocabulary sweep is 13 comment sites, not the 4 the source spec enumerated.** Beyond the
spec's `TextureAssetHandle.h:10-13`, `IScene.h:208-209`, `IScene.h:260-261`, `IScene.h:347` and
`PreparedStaticMesh.h:55-56`:

| Site | What it says |
|---|---|
| `ISceneView.h:14` | "(meshlets/vertices/indices)" |
| `ISceneView.h:79` | "The PSO follows the override" |
| `MaterialHandle.h:12` | "the (layer, type) pair that picks the PSO bucket" |
| `IScene.h:221` | "vertex / index / meshlet data" |
| `IScene.h:250` | "the bind-pose mesh whose meshlets and UVs every instance draws" |
| `IScene.h:272` | "cooked meshlets" |
| `IScene.h:356` | "every backend — **Metal** resolves the handle at dispatch" |
| `IScene.h:384` | "The PSO bucket derives from the material's *type*" |
| `IScene.h:428` | "vertex/index/meshlet data" |
| `PreparedStaticMesh.h:49` | "meshlet table" |

The three `PSO bucket` sites are the interesting ones, because task 3 removes the *type* from the
public surface while these comments keep the *concept*. Bucketing by material is real and
observable — an override may move an instance between buckets, which is what `ISceneView.h:79`
exists to say — so the comments stay and stop spelling it as a D3D acronym.

Four further grep hits are **stated non-goals, not misses**: `IRenderTarget.h:29-30`,
`IGraphics.h:63-64`, `IGraphics.h:68-79` and `IScene.h:40`.

**`MaterialType` stays public and stays as it is.** It is genuinely used by a public declaration
(`MaterialHandle.h:10`), it is referenced outside bgl
(`libs/gamelib/tests/src/AssetManager_test.cpp`, `examples/bgl_gpu_assert/src/main.cpp`), and its
enumerators — `kNull`, `kAssert`, `kPBR`, `kLoosePbr` — are shading vocabulary, not pipeline
vocabulary. `PsoType` is the only member of `IDL_PUBLIC_CPP_SOURCES` that moves.

**`docs/idlgen.md:81-85`'s rule already does half this job.** `IDL_PUBLIC_CPP_SOURCES` holding no
structs is what keeps a meshlet-shaped GPU layout out of the public headers; it is currently
justified by per-backend layout divergence, which is the same reason one layer up.

## What changes, and what could break

| Area | Change | Risk |
|---|---|---|
| `libs/bgl/include/bgl/` → `libs/bgl_intfc/include/bgl/` | 22 headers moved (23 less `PsoType.h`, deleted by then) | **Silent when wrong.** No source file names the old path, but CMake and clang both accept a `-I` onto a directory that does not exist, so a missed site neither fails the configure nor the build. The site list above is the check. |
| `libs/bgl/CMakeLists.txt` `:9-12,22,31,107,216,297` | glob, `source_group`, four include directories | Silent, as above. The glob is the exception: pointed wrong it yields no headers, which an IDE shows and a build does not. |
| `libs/bgl/idl/src/CMakelists.txt:7`, `scripts/gen_idl.py:64` | IDL public output directory | **The one real hazard.** Move one and not the other and the two generation paths disagree; the build still succeeds because the stale committed header is still on the include path. Caught by the `just idl`-then-`git status` gate. |
| `.gitattributes:104-105` | `PsoType.h` line deleted (task 3), `MaterialType.h` repathed (task 5) | Low; those two lines are the only `libs/bgl/include` entries, and a wrong line ending on a generated header shows as a spurious diff. |
| `libs/bgl/src` + `libs/bgl/tests/src` | `PsoType` → `bgl::idl::PsoType`, 47 + n sites | Low but wide. Does not compile if wrong. |
| `libs/bgl/include/bgl/*.h` comments | 13 rewordings, one rename | Rename touches `Uniforms.h:258`; the rest is comments. |
| `libs/assetlib_structs/.../Mesh.h` | one comment on the dual form | None. Layout untouched; `sizeof(Submesh) == 96` still asserts. |
| `docs/` | 4 corrections, 3 additions, 1 new ADR, 1 spec committed | None to the build. |

The thing that would actually hurt: the hoist landing before the `PsoType` demotion, which would
move `PsoType.h` into the contract target and then move it straight back out. The task order below
exists mostly to prevent that and the rebase churn around it.

## The tasks, in order

Bottom-up is not the ordering axis here — nothing crosses a layer boundary. The axis is **edit in
place, then move**: tasks 1–4 touch files where they currently live, task 5 moves them, task 6
repoints what task 5 broke.

### 1. Name both shipped backends in the docs

`docs/rhi.md:5` and `docs/bgl_api.md:6` frame `bgl_d3d12` as *the* concrete backend; name both, and
say which seam each layer is — the RHI is API-agnostic *among APIs with bindless and mesh shaders*,
which is the sentence that would have answered this whole question without a survey.
`docs/slang_shaders.md:3` says every shader is compiled to DXIL; Metal compiles MSL at runtime.
`libs/bgl/CLAUDE.md` § Subsystems gains a `bgl_metal` entry beside `bgl_d3d12`.

Independent of everything else in this feature; wrong today on its own terms.

**Gate.** Every corrected sentence cites the `file:line` that makes it true, in the PR body, so the
review checks claims rather than prose. `libs/bgl/CMakeLists.txt:64-68,125-126,147-149` is the
evidence for all four.

### 2. Record that `Submesh`'s plain ranges are load-bearing

A comment in [Mesh.h](../../libs/assetlib_structs/include/assetlib_structs/Mesh.h) on the dual form
— the meshlet arrays are what the shipped renderer draws; the plain vertex/index ranges exist so a
non-mesh-shader renderer can consume a `.bmesh`, and dropping them is a `c_BakeToken` bump *and* a
re-cook of every asset in every project. The same fact in
[docs/geometry_layout.md](../geometry_layout.md) beside the meshlet-partitioning bullet and in
[docs/asset_standards.md](../asset_standards.md)'s static-mesh conventions.

Second because it is the highest value per line in the feature and depends on nothing: it is the
change that stops a future cook-size pass from turning the port into a re-cook.

**Gate.** `just build && just test` — `Mesh.h` is in `assetlib_structs`, which most of the tree
includes, so a malformed comment is not the failure mode; the failure mode is nothing, and the
`sizeof(Submesh) == 96` assertion at `:44` still holds untouched. Reviewed for whether the comment
states the constraint or narrates the code.

### 3. Demote `PsoType` out of the public surface

Move `PsoType.slang` from `IDL_PUBLIC_CPP_SOURCES` to `IDL_CPP_SOURCES`
(`libs/bgl/idl/src/CMakelists.txt:24-57`), which re-emits it into `${CMAKE_BINARY_DIR}/generated/idl`
under `bgl::idl`. Delete the committed `libs/bgl/include/bgl/PsoType.h` and the unused
`#include <bgl/PsoType.h>` at `IScene.h:14`. Requalify the 47 references across 10 source files and
the 8 test files. Fix the comment at `libs/bgl/idl/src/CMakelists.txt:50-53`, which is wrong about
callers. Update `.gitattributes:104`, `docs/bgl_api.md:88,132` and `docs/idlgen.md:84`.

Before task 5, so the hoist never carries this header.

**Gate.** `just build && just test`. A missed requalification does not compile. What catches a
requalification that compiles but changed a *value* is the eight suites that name the enumerators
by hand — `PsoSelection_test`, `CompactInstances_test`, `CullInstances_test`,
`TransparentDepthKeys_test`, `PerViewCulling_test`, `HistogramInstances_test`, `SkinnedGeom_test`,
`MeshDelete_test`. **Not** the `c_Psos` `static_assert`: `ForwardPass.cpp:196-199` is
`none_of(c_Psos, ...pixelSrc.empty())`, and `docs/passes.md:362` says so outright — *"a
`static_assert` catches an empty row but not a misordering."* This task must not reorder the
enumerators, and nothing mechanical would catch it if it did. Plus:
`grep -rn "bgl::PsoType\|<bgl/PsoType.h>"` returns nothing.

### 4. The neutrality sweep over the public headers

Rename `TextureAssetHandle::srvBindlessIndex` to a name that says what it is for rather than what it
is made of, reword its comment to *what the renderer needs to reach this texture from a shader,
implementation-defined*, and update the one reader at `libs/bgl/src/uniforms/Uniforms.h:258`. Reword
all 13 sites tabulated in the survey: keep every throw contract and every postcondition, stop naming
the mechanism. A renderer with no meshlets still has a ceiling, and the contract is about the
ceiling. `IScene.h:356` stops naming Metal. The three `PSO bucket` comments keep the concept and
drop the acronym. One advisory comment on `GraphicsOptions`' capacities (`IGraphics.h:68-79`) and
`SceneDesc::initialMeshlets` (`IScene.h:40`) saying a renderer may ignore a capacity it has no
equivalent for.

Before task 5 for the same reason as task 3: edit in place, then move.

**Gate.** `just build && just test` — the rename does not compile if a site is missed, and nothing
else in the task can change behaviour. `just format` on every touched header. Then the grep from
Acceptance, which must come back to exactly the three non-goal sites named there and nothing else.
That grep is the gate this task is measured by, so it is run and its output pasted into the PR.

### 5. Hoist the contract into `libs/bgl_intfc`, with its self-check

`git mv libs/bgl/include libs/bgl_intfc/include`. New `libs/bgl_intfc/CMakeLists.txt`: the INTERFACE
target linking `core` and `assetlib_structs`, plus `bgl_intfc_selfcheck`, a STATIC library over one
TU that includes `<bgl/bgl.h>`. `add_subdirectory(libs/bgl_intfc)` before `libs/bgl` in the root
`CMakeLists.txt:63-66`. In `libs/bgl/CMakeLists.txt`: the `BGL_SHARED_HEADERS` glob (`:9-12`) and its
`source_group` (`:22`) repointed, `bgl_objects` linking `bgl_intfc` PUBLIC instead of declaring the
include directory itself (`:31`), and `bgl`'s own include directory (`:107`) likewise. **Both
backends too** — `libs/bgl/src/d3d12/CMakeLists.txt:29` and `libs/bgl/src/metal/CMakeLists.txt:27`
each declare the same directory by relative path, and only one of the two is built on any given
machine, so the other's breakage surfaces in CI rather than locally. `gamelib`
links `bgl_intfc` for the headers and `bgl` for the symbols, naming the contract rather than one
renderer. `IDL_PUBLIC_CPP_OUT_DIR` (`libs/bgl/idl/src/CMakelists.txt:7`) and `PUBLIC_CPP_OUT_DIR`
(`scripts/gen_idl.py:64`) move together, as does `scripts/gen_idl.py:7`'s docstring. `.gitattributes:105`
repathed — after task 3 it is the only `libs/bgl/include` entry left.

*Corrected during task 6:* the plan named `docs/idlgen.md` as the one doc linking the old path. It
is **eleven files** — `bgl_api.md`, `rhi.md`, `gfx_debug.md`, `taa.md`, `vat.md`, `skinning.md`,
`passes.md`, `shader_cache.md`, `asset_standards.md`, `idlgen.md` and
`.claude/agents/bcp-docmap.md` — carrying roughly fifty markdown links between them, every one of
which a `git mv` silently breaks because nothing resolves a markdown link at build time. The two
files under `docs/plans/` are deliberately **not** repathed: [CLAUDE.md](../../CLAUDE.md) files an
ADR as a record of a conversation on a date, so it keeps the path that was true when it was
written.

The single largest task, and the one that must not be split: a half-moved header set does not build.

**Gate.** All four of Acceptance's mechanical checks, and this is the task they exist for.
`bgl_intfc_selfcheck` builds; a deliberate `#include "scene/Scene.h"` added to a public header makes
it fail, and that is shown in the PR body before being reverted. `just idl` then `git status` clean.
`git diff --stat` shows no client source file. Full build and test on Metal locally; CI is the gate
for DX12, and the PR says so rather than claiming a Windows run that did not happen.

### 6. The record

An ADR under `docs/plans/` on renderer portability — the layer decision and the two rejections
(ADR-1), and why Metal was added, since there is currently no ADR on portability at all. A short
note in [docs/bgl_api.md](../bgl_api.md) that the public surface stays renderer-agnostic: one or two
sentences, not a rulebook. `docs/idlgen.md:81-85`'s existing rule gains its *why* — holding structs
out of `IDL_PUBLIC_CPP_SOURCES` is what keeps a meshlet-shaped GPU layout out of the public headers.

And `docs/specs/webgpu_renderer.md` is committed. It exists today only as an untracked file in a
working clone, which is precisely the state a spec should not be in: [CLAUDE.md](../../CLAUDE.md)
files `docs/specs/` as one file per problem we have decided **not** to solve yet, and the WebGPU
renderer is not landing. It goes in with its "Changes to land now" and "Documentation changes"
sections replaced by a line saying they landed here, its four factual corrections from this survey
folded in (the leak list was incomplete; `PsoType`; the two hardcoded IDL output paths; the
`Uniforms.h` reader), and its design and open questions kept intact.

Last, because it cites ~40 `file:line` pairs into `libs/bgl/include/bgl/`, every one of which task 5
moved.

**Gate.** Every path and line reference in the committed spec resolves — checked by script over the
markdown links, not by eye. The plan you are reading is deleted in this task, per
[bcp-feature § 5](../../.claude/skills/bcp-feature/SKILL.md); the ADR and the docs are what outlive
the feature.
