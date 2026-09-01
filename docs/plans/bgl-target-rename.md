# bgl target rename — implementation plan

## Context

`bgl` names two different things depending on where you read it. Every client source file writes
`#include <bgl/IGraphics.h>` and `bgl::IGraphics`, so in client code the word means *the contract*.
In CMake it means one implementation of that contract, and the contract itself is called
`bgl_intfc` — an abbreviation that appears in no source file, no include line and no namespace.

That split was made knowingly in #527, which moved the public headers out of `libs/bgl_extended` "so that
`bgl` names one implementation of this interface instead of naming both", and paid for it with the
`_intfc` suffix. The suffix is the half that was never right: every engine that has made this split
gives the bare name to the contract and names implementations for what they are built on — Unreal's
`RHI` module against `D3D12RHI`/`VulkanRHI`/`MetalRHI`, Godot's `RenderingDevice` against
`RenderingDeviceDriverVulkan`/`D3D12`/`Metal`, wgpu's `wgpu-hal` against `hal::vulkan`/`hal::metal`.

Why now: a second renderer is coming. WebGPU has no bindless heap and no mesh stage, so it cannot be
an RHI backend — ROADMAP draws the RHI at exactly that bar — and it lands as a second renderer above
`bgl`'s public interface, serving both the browser and, on a native device that misses the bar, a
fallback tier. Naming it is cheapest while there is still only one renderer to move, and it cannot
be named sensibly against a sibling called `bgl`.

## Decisions

- **ADR-1 — the contract takes the name `bgl`, and the renderer moves to `libs/bgl_extended`.** The bare
  name goes to the thing clients already spell `bgl`, which makes `<bgl/…>`, `bgl::` and `BGL_API`
  all resolve to one target instead of three ideas. *Rejected: renaming only the interface to
  `bgl_api` and leaving the renderer as `bgl`. It is far cheaper, but `libs/bgl` has to be vacated
  before the contract can occupy it, so the two halves are one change — and it preserves exactly the
  ambiguity being deleted.*

- **ADR-2 — the renderer is `bgl_extended`: the extended feature tier.** It is the tier that
  requires bindless resource access and a mesh stage; `bgl_wgpu` will be the baseline tier that does
  not. The precedent is graphics-native — `Direct3D 9Ex`, GL's `EXT`. *Rejected: `bgl_native`,
  because `bgl_wgpu` ships natively too and the contrast is simply false. Rejected: `bgl_gpuir` and
  `bgl_gpudriven`, because `bgl_wgpu` also renders instances on the GPU — WebGPU has indirect draws
  and compute — so they name something both renderers do, and `IR` already means Slang intermediate
  representation in this tree. Rejected: `bgl_meshlet` and `bgl_bindless`, which name capabilities
  `bgl_wgpu` categorically lacks and so age better than a relative term; the tier reading was
  preferred over the mechanism, and the expiry only bites if WebGPU ever clears the bar, at which
  point one of the two renderers is deleted rather than renamed. Rejected: `bgl_ex`, which was the
  same decision abbreviated — the change exists to delete an abbreviation nobody can expand.*

- **ADR-3 — a target is renamed only if it names the renderer.** `bgl_objects` and `bgl_tests` are
  parts of the renderer and become `bgl_extended_objects` and `bgl_extended_tests`; `bgl_intfc_selfcheck`
  becomes `bgl_selfcheck` with the contract. `bgl_d3d12`, `bgl_metal` and `bgl_d3d12_agility` are
  untouched — they name the graphics API they are built on, which is the convention every engine
  above uses and which stays true. `bgl_idl_generate` and the shader-copy targets are untouched:
  they name a build step over the whole `bgl` family, and the IDL emits into the contract as well as
  the renderer. *Rejected: prefixing everything under `libs/bgl_extended` uniformly, which would rename
  the two backends away from the API each one actually is.*

- **ADR-4 — the runtime log becomes `bgl_extended.log`.** Both `Graphics_d3d12.cpp` and
  `Graphics_metal.cpp` open it, both live in the renderer, and a second renderer selected at runtime
  cannot share the name. *Rejected: keeping `bgl.log`, which is the most-typed filename in
  debugging — but it is a renderer artefact wearing the contract's name, which is the confusion this
  change exists to delete.*

- **ADR-5 — `bgl_wgpu` is agreed and recorded, not built.** ROADMAP's browser-target entry gains the
  name and the fallback role it now has. *Rejected: cutting a `libs/bgl_wgpu` skeleton to prove the
  second-renderer seam, which adds a target that compiles nothing and a DLL nothing links.*

## Non-goals

- No `bgl_wgpu` target, directory, stub or `CreateGraphics`. Nothing is built for it.
- No decision on configure-time vs runtime renderer selection, and none on whether `RENDERER_BACKEND`
  ever becomes a runtime probe. The names must merely survive coexistence; the mechanism is the wgpu
  feature's ADR.
- No ADR restoring the portability rule that #534 deleted on review. It survives as ROADMAP's
  Guiding Constraint, and reinstating it is that feature's argument to make, not this rename's.
- The IDL stays under the renderer, even though it emits public headers into the contract. That
  seam is pre-existing and moving it is a separate change.
- No behaviour change beyond the log filename in ADR-4. Everything else here is a rename or a
  path, and no shipped code path is added, removed or reordered.

## Acceptance

- `just build` configures and links clean; `just test` green on every suite, `bgl_extended_tests` included.
- `bgl_selfcheck` still fails the build when a public header reaches into a renderer's internals —
  verified by temporarily including a `libs/bgl_extended/src` header from a public one and confirming the
  break.
- `grep -rn "bgl_intfc" --exclude-dir=build --exclude-dir=.git .` returns nothing.
- Every markdown link into `libs/bgl_extended/…` resolves, and `just count` reports `bgl` and `bgl_extended` as two
  modules with no `(unclassified)` rows.

## Commits

1. `docs(plans): plan the bgl target rename` — this file. Gate: it is the PR's first commit.
2. `refactor(bgl): the renderer moves to bgl_extended` — `libs/bgl` becomes `libs/bgl_extended`;
   targets `bgl`, `bgl_objects` and `bgl_tests` become `bgl_extended`, `bgl_extended_objects` and
   `bgl_extended_tests`; the runtime log becomes `bgl_extended.log`; every consumer, script and doc
   path follows. `bgl_intfc` is untouched and still builds. Gate: `just build && just test`.
3. `refactor(bgl): the contract takes the name bgl` — `libs/bgl_intfc` becomes `libs/bgl`; targets
   `bgl_intfc` and `bgl_intfc_selfcheck` become `bgl` and `bgl_selfcheck`. Gate:
   `just build && just test`, plus the selfcheck verification above.
4. `docs: record bgl_wgpu as the baseline tier` — ROADMAP's browser-target entry, `docs/bgl_api.md`'s
   neutrality rule, `libs/bgl_extended/CLAUDE.md`. Gate: link resolution, `just count`.
