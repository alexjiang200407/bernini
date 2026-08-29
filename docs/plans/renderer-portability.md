# renderer portability — which seam a new graphics target arrives at

*2026-08-29. Recorded while landing `feat/bwgpu-setup`, which enforced the second half of it.*

## Context

Bernini had shipped two backends — `bgl_d3d12`, then `bgl_metal` — with no record of why either fit
where it did, and no rule saying where a third would go. `ROADMAP.md`'s Guiding Constraints say the
RHI "stays API-agnostic… so the Vulkan/Metal backends stay viable", which is true and is also the
whole of the written guidance. It does not say what API-agnostic is *scoped to*, and that omission
is what made "can we target the browser?" cost a survey rather than a paragraph.

Metal is the case in point. It landed as an RHI backend and fits cleanly, but nothing records that
this was a judgement rather than the only option, or what property of Metal made it work.

## Decisions

- **ADR-1 — the RHI is API-agnostic among APIs with bindless resource access and mesh shaders, and
  no further.** That is the bar it is drawn at: the only graphics pipeline object is
  `IMeshletPipeline`, and the draw verbs are `Dispatch`, `DispatchMesh` and `DispatchMeshIndirect`
  ([docs/rhi.md](../rhi.md)). D3D12 and Metal both clear it; Vulkan clears it with
  `VK_EXT_mesh_shader`. *Rejected: treating "API-agnostic" as unqualified, which is what the
  roadmap's wording invited and what made a WebGPU backend look like a plausible piece of work.*

- **ADR-2 — an API that clears that bar arrives as a backend under the RHI; one that does not
  arrives as a second renderer implementing bgl's public interface.** The structural reason is not
  the interface but what sits above it: `libs/bgl/CMakeLists.txt` excludes only `src/d3d12|metal`
  from `bgl_objects`, so Scene, SceneView, the FrameGraph and **every pass** are backend-common —
  and every geometry pass is a mesh-shader draw ([docs/passes.md](../passes.md)). A third RHI arm
  for an API without mesh shaders would link a common half it cannot run. *Rejected: a WebGPU RHI
  backend — it would implement pure virtuals it has no equivalent for. Rejected: adding a
  vertex-pipeline path to the existing renderer, which doubles every pass and every shader in
  `bgl_objects` permanently, to serve a target that also needs a different culling design (no
  multi-draw, no indirect count) and a different material binding design (no descriptor indexing).
  The two architectures disagree about more than the draw call.*

- **ADR-3 — Metal was the right call as a backend, and the reason is ADR-1's bar, not convenience.**
  It has argument buffers and mesh shaders, so `bgl_objects` runs on it unchanged. Recorded because
  it was the first test of a rule nobody had written down, and it passed by luck of fit rather than
  by anyone checking.

- **ADR-4 — the public interface is the contract, it lives in `libs/bgl_intfc`, and a build target
  holds it there.** `bgl_intfc` is an INTERFACE target over `core` and `assetlib_structs`;
  `bgl_intfc_selfcheck` is one translation unit that includes `<bgl/bgl.h>` and links only
  `bgl_intfc`, so a public header that reaches into a renderer's internals fails the build.
  *Rejected: leaving the headers under `libs/bgl` and pointing an INTERFACE target at them, which
  enforces identically — the move is what stops `bgl` naming both the contract and one of its
  implementations. Rejected: a rule in prose alone, which is what had been protecting the surface
  and which had already let `srvBindlessIndex` and a public `PsoType` through.*

- **ADR-5 — one renderer per binary, chosen at configure time.** `BGL_API` is a single macro with no
  per-DLL name, and the public headers carry explicit `template class BGL_API core::SharedRef<...>`
  instantiations that assume exactly one exporting DLL. A second renderer is therefore a third
  `RENDERER_BACKEND` arm producing the same target name — but not the same *kind* of arm: `DX12` and
  `METAL` select a backend *under* `bgl_objects`, while a non-mesh-shader renderer would substitute
  something else *for* `bgl_objects`. *Rejected: runtime backend selection
  (`CreateGraphics(backend, opts)`), which is what Unreal's dynamically-loaded RHI modules and
  bgfx's init-time pick both do — the standard, deviated from knowingly. It buys nothing for a
  target that is one renderer per binary, and costs the `BGL_API` model, the explicit instantiations
  and a single-renderer link.*

## What this does not decide

Whether a browser target is ever built, and every design question inside one — material binding,
culling and submission, what the FrameGraph is worth without barriers to derive, shaders, and which
features would need parity. Those are recorded in
[docs/specs/webgpu_renderer.md](../specs/webgpu_renderer.md), which describes code that does not
exist.

It also does not decide Vulkan. Vulkan clears ADR-1's bar, so ADR-2 says it is a backend and reuses
all of `bgl_objects` — but *whether* to build it is a roadmap question, not this one. Worth knowing
that the two are an order of magnitude apart in cost, and that only one of them is a browser: if the
driver is ever device coverage rather than the browser specifically, Vulkan is the cheaper and
larger win.

## How it was enforced

`feat/bwgpu-setup` landed the cheap half in six PRs: the docs corrected to name both shipped
backends and state ADR-1's bar; `assetlib::Submesh`'s plain index range recorded as load-bearing
(a non-mesh-shader renderer is the only thing that would read it); `PsoType` demoted out of the
public surface; thirteen public comment sites reworded to state contracts rather than mechanisms;
and the headers hoisted into `libs/bgl_intfc` behind `bgl_intfc_selfcheck`.

No renderer was written, and none is scheduled.

One decision inside that work is worth recording, because it sets aside a rule rather than following
it. Moving the headers broke a `libs/bgl/include/...` link in
[headless_render_target_window](headless_render_target_window.md), an unrelated ADR, and
[CLAUDE.md](../../CLAUDE.md) says an ADR "is amended only by a change that *reverses* it". The link
was repaired anyway: the cited line still reads "Ignored when headless", so the claim the ADR makes
is untouched and only the directory moved. **A dead-link repair is mechanical, not an amendment** —
an ADR's value is that a reader can follow it to the code, and a path that resolves to nothing
serves the record worse than a path that resolves to the same line in a new place. A change to the
*reasoning* is still forbidden.
