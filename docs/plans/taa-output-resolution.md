# taa-output-resolution — implementation plan

## Context

The history, the scene colour and the backbuffer are all one size. `RenderTargetDesc{width, height}`
([libs/bgl/include/bgl/IRenderTarget.h](../../libs/bgl/include/bgl/IRenderTarget.h)) sizes a target
at creation, `RenderTargetBase::ResizeBackbuffers` is the only resize entry point, and both backends
allocate every attachment — backbuffers, depth, velocity, scene colour, outline mask and both TAA
histories — at that one number. The editor's Render Scale multiplies it
(`RenderTargetWindow::SyncSize`), and the presented image is stretched back over the window by the
compositor (`DXGI_SCALING_STRETCH`; a `CAMetalLayer` drawable smaller than its bounds).

So below 1.0 the accumulation happens on the render grid and a bilinear tent carries it to the
panel. PR #372 measured what that costs. A moving mesh re-fetches its history at a fractional texel
offset every frame; Catmull-Rom loses a little contrast per fetch near Nyquist, and after ~10 frames
at 0.9 feedback a 1–3 px feature has faded toward its surroundings. On the coyote at 960×540 into a
1080p panel, mean |Δ| against a 4× supersampled truth: near, interior sharpness under motion 0.92 of
the held frame; far (~30 px tall), still camera 0.53 against a raw no-AA 0.65, and **1.05 under a
0.5°/frame orbit — worse than no antialiasing at all**. It is speed-independent (0.920 / 0.910 /
0.909 at 1×/2×/4× playback) and fixed in *texels*, which is why the same scene at 4K reads as
flawless. A resolve knob can shave it — #372's motion-gated blend took 1.05 to 1.00, capped by the
hashed pan's trailing-band guard — but only accumulating on a finer grid than the render attacks
the mechanism.

Why now: the roadmap's crowd is distant units, and every one of them pays this at native 1080p.

This supersedes `docs/plans/taa-output-resolution-proposal.md`, which was written at the end of #372
to carry those measurements forward and explicitly deferred every decision to this grill. It is
deleted in the same commit; what it measured is above, and what it proposed is decided below.

## Decisions

- **ADR-1 — A render target carries two sizes.** The *output* size is what is presented, captured
  and accumulated on; the *render* size is the output size scaled by `RenderTargetDesc::renderScale`
  and is what the geometry passes see. Scene colour, depth, velocity and the outline mask are
  allocated at render size; both TAA histories and the backbuffer at output size. `Resize` takes the
  output size and derives the render size. At scale 1.0 the two coincide and nothing below changes
  shape. *Rejected: a second render target for the output grid, because the history ping-pong, the
  frame counter and the jitter index are per-target state the resolve needs on one object, and two
  targets would have to agree on all of it every frame.*

- **ADR-2 — The resolve maps render → output at any ratio, and the present-time stretch goes
  away.** `DXGI_SCALING_STRETCH` and the undersized `CAMetalLayer` drawable stop being how a render
  scale reaches the screen: the swapchain is the window, always. Above 1.0 the resolve becomes a
  jitter-weighted accumulating downsample, which is a better filter than the compositor's and one
  code path instead of two. *Rejected: keeping the compositor stretch above 1.0 — what the proposal
  wrote — because it puts a conditional in every attachment's allocation and leaves two unrelated
  mechanisms by which a scale reaches the panel, only one of which is measured.*

- **ADR-3 — The current frame's contribution is weighted by how near its jitter landed to the output
  pixel's centre**, a Gaussian of ~0.4 render pixels, **normalized over the sub-pixel phases one
  render sample serves**. Eight phases then fill a grid four times as dense: an output pixel no
  sample landed near this frame takes almost none of the current frame rather than a full-weight
  wrong colour. The normalizer is what makes ADR-6 reachable — at output = render every pixel sits
  the same distance from its sample, so the kernel is a per-frame constant, and normalizing it away
  leaves the weight exactly 1 and the scale-1.0 image untouched — returned as unity rather than
  divided out, for the reason ADR-6 records. It also states the kernel's job
  honestly: redistribute the frame's weight between sub-pixel phases, not change how much of the
  frame is taken. *Rejected: switching the kernel off when the grids coincide, because a scale of
  1.01 would then jump from full weight to as little as a fifth of it. Rejected: taking the nearest
  render sample unweighted, because at 2× three of every four output pixels are reconstructed from a
  sample up to a full render texel away, at full blend weight.*

- **ADR-4 — The clamp box stays a render-resolution 3×3 around the mapped sample.** So do the depth
  read for `CameraMotion` and the object-motion discriminator. *Rejected: a 3×3 on the output grid,
  because at 2× it is nine taps of an upscaled image — it can report no colour the render
  neighbourhood did not already contain, while making the variance store's "no survivor in the 3×3"
  event mean something different from what it was tuned on.*

- **ADR-5 — The jitter sequence length is per target: `8 × ceil(output/render per axis)²`, clamped
  to 32, and 8 whenever the render grid is at least as dense as the output grid.** Eight positions
  walk one render pixel; four output sub-pixels want four times as many, and supersampling already
  oversamples every output pixel. The offset stays ±0.5 *render* texel and the per-target index of
  #372 is unchanged. *Rejected: raising `c_JitterSequenceLength` to 16 globally, because it changes
  which offset frame N renders with at scale 1.0 — the image differs, every golden is re-pinned and
  ADR-6's gate is gone before a single 2× figure can be trusted.*

- **ADR-6 — Bit-identity at scale 1.0 is the first gate.** Where the two grids coincide the
  reconstruction kernel is identically one, the nearest sample is the pixel's own texel, and the
  neighbourhood clamp and both motion discriminators read the same texels they read before — so the
  arithmetic is the same arithmetic, and every existing `[taa]` and `[hashedalpha]` figure holds
  without being re-measured. Held: all 58 `[taa]` captures — pans, ghosts, parallax, material edits,
  animation and the resolution sweep — are byte-for-byte what they were before the resolve moved.
  Nothing at 2× is read until that is true.

  *Reaching it took three goes, and the two failures are worth keeping. Twenty captures differed
  until the interpolated uv was kept rather than reconstructed where the grids coincide. Four
  survived that, and were briefly written off in this document as the backend contracting different
  multiply-adds — wrongly: the cause was `PhaseWeight` dividing a kernel by its own single-phase
  mean, which a backend may implement as an approximate reciprocal, so `x/x` landed a bit either
  side of one. Returning unity at one phase rather than computing it took the residue to zero. The
  lesson generalises: a property this gate rests on has to be structural, not arithmetic that ought
  to cancel.*

- **ADR-7 — A 4×-supersampled truth harness is built, and the far-mesh figure asserts.** No such
  harness exists today; the numbers in Context were measured ad hoc, and the `[resolution]` sweep in
  `HashedAlpha_test.cpp` only `WARN`s. *Rejected: `WARN`-only to match that sweep, because a golden
  image and a warning both pin "unchanged" — neither can show that the far mesh stopped losing
  detail, which is the only claim this change makes.*

- **ADR-8 — Captures are always output size.** A screenshot at Render Scale 2.0 becomes window-sized
  where today it is twice that. *Rejected: capturing before the resolve's downsample to preserve
  that, because it needs a capture path that does not read the presented backbuffer, and
  "`PostProcess` is the only writer of the backbuffer, and a screenshot is what was presented" is an
  invariant `docs/taa.md` and `docs/bgl_api.md` both state.*

- **ADR-9 — With TAA off, or on the first frame after a resize or a toggle, the output grid takes
  the scene colour filtered up.** There is no history to reproject, and the backbuffer's size must
  not depend on a runtime toggle. *Rejected: allocating the backbuffer at render size when TAA is
  disabled, because `SetTaaEnabled` exists precisely so a viewport can be compared against itself
  without recreating anything.*

- **ADR-10 — The kernel's width is a per-target knob, in output pixels, defaulting to 0.4.**
  `RenderTargetDesc::taaReconstructionWidth` and `IRenderTarget::SetTaaReconstructionWidth`, and a
  Render Scale-shaped entry in the editor's Render menu beside it. Narrowing it sharpens a held
  frame without limit — a still pixel eventually sees every phase — while lengthening the wait a
  moving pixel has for the phase that serves it, and only a moving image shows the second half. That
  is a judgement to be made by eye at a render scale, on the scene in front of you, which is exactly
  what the Render Scale menu already exists for. It reallocates nothing and keeps the accumulation,
  so it can be swept mid-scene. *Rejected: leaving it the measured constant, because the measurement
  that set it could only see the half a still frame shows. Rejected: exposing the jitter sequence
  length instead, whose effect appears over tens of frames rather than in one a viewer can look at.*

## Non-goals

- **The vendor upscalers** — FSR 2/3, MetalFX Temporal, DLSS, XeSS. They replace `TaaResolve.slang`
  whole, and with it the variance store that makes hashed alpha converge, the object-motion
  discriminator and every per-tag figure. Revisit once this exists as the reference to compare
  against.
- **Post-resolve sharpening** (RCAS-style). Additive to either path, restores no sub-pixel
  structure.
- **A supersampled history at scale 1.0** — history at 2× the render size even when output equals
  render. A switch on this same design, worth measuring after it lands, not with it.
- **What the geometry passes render, or how.** The render size stays theirs; no pass below the
  resolve learns that an output grid exists.
- **The base blend weight and #372's motion-gate constants.** Whatever the tail does at 2× is
  measured here and tuned in its own change.

## Acceptance

- `just run bgl_tests -- "[taa]"` at scale 1.0: every measured figure passes unchanged, and a
  byte-level capture diff against the pre-change resolve is empty across all 58 captures. This gate
  passes before any 2× figure is read.
- A new `[taa][render][truth]` case renders a 4× supersampled truth, box-downsamples it to output
  size, and measures mean |Δ| over the subject. The gates are **relative**, because a bound copied
  off one machine's measurement is a bound about that machine: under a slow orbit at half render
  scale, the accumulation's error must be **at or below the same scene drawn with no antialiasing at
  all** — which is the state 1.05-against-0.65 says it fails today — and the detail a held frame
  carries must survive motion to within **5%**, against the 8% measured in #372. The absolute
  figures are reported in the PR body, not asserted.
- `HashedAlpha_test`'s `[resolution]` sweep gains a 2× rung, and the hashed coverage ladder runs at
  both scales — the LOD-bias interplay is a render-size decision and must be shown not to have moved.

  *Amended during implementation: it moves, and not because of this change.* At half render scale the
  far rung's coverage against its blend reference climbs from 1.05 to 1.57. The sweep is what
  exonerates the reconstruction: rendering at 128 and 64 with nothing reconstructed anywhere, the
  same ratio is already 1.29 and 1.62. Halving the render size doubles every footprint and coverage
  rises with it, so the ladder's ceiling is per-scale and its floor is not.
- `just test` green, and `just run bgl_tests -- --gpu-validation` on D3D12 plus
  `METAL_DEVICE_WRAPPER_TYPE=1 MTL_SHADER_VALIDATION=1` on Metal, because attachments, descriptors
  and shaders all change.

## Commits

1. `docs(plans): plan TAA at output resolution` — this file, and the deletion of the proposal it
   supersedes. Gate: the boundaries are the first thing in the diff.

2. `refactor(bgl): a render target owns its size` — `m_Width`/`m_Height` and the `GetWidth`/
   `GetHeight` overrides move out of both backends into `RenderTargetBase`, so there is one place
   the render size can later be derived in and the two backends cannot disagree. No behaviour
   change. Gate: `just test bgl` — every existing suite, unchanged.

3. `feat(bgl): a render target carries a render size beside its output size` —
   `RenderTargetDesc::renderScale`, `GetRenderWidth`/`GetRenderHeight`, the attachment split (scene
   colour, depth, velocity, outline mask at render size; backbuffer and both histories at output
   size), `IGraphics::SetRenderScale`, `RenderContext::Draw` scaling the job's viewport onto the
   render grid, and the per-target jitter sequence length. The resolve still runs at render size, so
   at any scale but 1.0 the image is wrong until commit 4 — the sizes are what this commit pins.
   Gate: a new `[taa]` case asserting the two sizes and their derivation, plus every existing
   `[taa]`/`[hashedalpha]` golden unchanged at the default scale.

4. `feat(bgl): the TAA resolve accumulates on the output grid` — `TaaResolve.slang` takes both texel
   sizes and the jitter in render texels, maps each output pixel to its nearest jittered render
   sample, weights it by ADR-3's normalized kernel, keeps the clamp box and both motion
   discriminators on the render 3×3, and Catmull-Roms the history in output texels. `PostProcess`
   filters when the grids differ and dilates the outline mask at render size. `docs/taa.md` and
   `docs/passes.md` in the same commit. Gate: `just run bgl_tests -- "[taa]"` — goldens unchanged at
   scale 1.0, and a new upscale case that resolves to the output grid.

5. `feat(bgl): a 4x supersampled truth to measure the resolve against` — `MeanAbsDiff` and a box
   `Downsample` in the test image helpers, the far-subject-under-orbit and near-interior cases of
   ADR-7, and the `[resolution]` sweep's 2× rung. Gate: `just run bgl_tests -- "[truth]"`.

6. `feat(editor): render scale is the target's, not the compositor's` — `RenderTargetWindow` passes
   the physical window size as the output size and its scale in the desc, the Render menu drives
   `SetRenderScale`, and the render-size fields it kept for the job viewport go. `docs/taa.md`'s
   Render Scale section in the same commit. Gate: `just test editor`, and the editor run at 0.5 /
   1.0 / 2.0.
