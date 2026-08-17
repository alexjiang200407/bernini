# TAA at output resolution — proposal

**Status: a proposal, not a plan.** Written 2026-08-16 at the end of PR #372, from what that PR
measured; nothing here has been grilled or agreed, and the grill comes before any code
(`bcp-grill`). It exists so the next conversation starts from the measurements rather than from
memory. Decisions and boundaries below are *proposed*; the ADRs are the grill's to write.

## Context — what is left after #372, and why it is resolution

Three resolve defects were fixed and measured in #372: the animating silhouette reprojecting from
the wrong neighbour, the jitter index shared across targets, and a still sky reporting motion. What
remains is one mechanism, and it is not a defect of any pass:

- **A moving mesh loses pixel-scale detail while it moves.** Its history is re-fetched at a
  fractional texel offset every frame; Catmull-Rom loses a little contrast per fetch at frequencies
  near Nyquist, and after ~10 frames of that (0.9 feedback) a 1–3 px feature has faded toward its
  surroundings and its silhouette wobbles with the fractional phase. Close up it is invisible
  (features are 10–50 px); far away the whole mesh *is* pixel-scale detail.
- Measured on the coyote at 960×540 (half render scale on a 1080p panel), mean |Δ| against a 4×
  supersampled truth over the mesh: near, interior sharpness under motion 0.92 of the held frame
  (0.95 at 4K); far (~30 px tall), still camera **0.53** (raw, no TAA: 0.65), orbiting 0.5°/frame
  **1.05 — worse than no AA**, frame-to-frame 0.22 against 0.02 at rest. Identical on master's
  resolve; the min/max box makes it worse (1.23); the motion-gated blend of #372 takes it to 1.00.
- It is speed-independent (1×/2×/4× playback: 0.920 / 0.910 / 0.909) and **fixed in texels**, which
  is why 4K, or 2× render scale on a 1080p panel, reads as flawless: the same features are twice
  the texels, out of the range where the loss bites, and what survives is physically half the size.

So the report — "static and skinned, only far away, moving camera, 1080p" — is the accumulation's
resampling loss on sub-pixel geometry. A resolve knob can shave it (the motion ramp is capped by the
hashed pan's guard); only accumulating on a finer grid than the render attacks the mechanism.

**What render scale is today.** `RenderTargetWindow` scales the *target* — backbuffer, depth,
velocity, scene colour, outline mask and both TAA histories are all allocated at the render size
(`RenderTarget_metal.cpp`, and the D3D12 twin) — and the presented image is stretched over the
window by the compositor (`DXGI_SCALING_STRETCH`; a `CAMetalLayer` drawable smaller than its
bounds). Below 1.0 that is a bilinear tent over a render-resolution accumulation: a game would call
it "performance mode without the upscaler", and nobody ships it. Above 1.0 it is supersampling paid
in full.

## The proposal — accumulate at output resolution (TAAU)

The history lives on the *output* grid; every geometry pass keeps rendering at the render size; the
resolve turns eight jittered render-resolution frames into an output-resolution accumulation. This
is what FSR 2, TSR and DLSS are structurally, kept inside the resolve this repo already measures.

1. **Two sizes on the target.** `RenderTargetDesc` gains a render scale (or the render size beside
   the output size). Scene colour, depth, velocity and the outline mask stay at render size; the
   two TAA histories and the backbuffer are output size; `Resize` takes the output size and derives
   the render size. The editor stops leaning on the present stretch: the target *is* the window
   (`RenderTargetWindow::SyncSize` passes the physical size, and the scale goes into the desc).
   At scale 1.0 the two sizes coincide and nothing below changes shape.

2. **The resolve runs at output size.** For each output pixel: reproject the output-res history by
   the velocity sampled at the mapped render uv (Catmull-Rom in output texels — the negative lobes
   matter more, not less, at 2×); build the 3×3 clamp box from the *render-res* neighbourhood
   around the mapped sample, exactly as today; take the current frame's contribution from the
   nearest jittered render sample, **weighted by how near that sample landed to the output pixel's
   centre** — a Gaussian of ~0.4 render px is what #311 measured for the still-image half of this
   ("each frame's share of the blend is weighted by where its jitter landed"), and at 2× it is what
   lets eight phases fill a grid four times as dense: a pixel no sample landed near this frame takes
   almost none of the current frame. Depth for `CameraMotion` and the object-motion discriminator
   read at render size; the variance store keeps living in the output history's alpha.

3. **The jitter sequence grows.** Eight Halton terms cover one render pixel; a 2× output grid wants
   its four sub-pixels each visited — sixteen at least (`c_JitterSequenceLength`), and the reasoning
   in `util/jitter.h` ("eight rather than sixteen because the blend fills in the rest") is what
   changes. Still ±0.5 *render* texel; the per-target index of #372 stays.

4. **PostProcess reads the output-res history** and writes the output-res backbuffer; the outline
   mask is dilated at render size and sampled scaled. Screenshots and readbacks are output size.
   The first frame after a resize or a TAA toggle takes the scene colour bilinearly upscaled, since
   there is no history to reproject.

5. **Everything measured stays measurable.** At scale 1.0 the resolve must be *bit-identical* to
   today's — that is the first gate — so every `[taa]` and `[hashedalpha]` figure holds by
   construction, and the resolution sweep instruments (`HashedAlpha_test`'s `[resolution]`,
   `TaaResolve_test`) gain a 2× rung.

## Alternatives, and why not first

- **FSR 2/3 (D3D12/Vulkan, open source), MetalFX Temporal (macOS), DLSS/XeSS (vendor).** They
  consume colour, depth, velocity and the jitter, and produce output-res colour — replacing
  `TaaResolve.slang` and with it every measured mechanism: the variance store that makes hashed
  alpha converge, the object-motion discriminator, the per-tag figures. Hashed alpha and the
  roadmap's dithered LOD crossfade would be re-tuned against a black box, and Metal would need a
  second one. Worth revisiting once the resolve above exists as the reference to compare against;
  not as the first move.
- **Sharpen after the resolve (RCAS-style).** Cheap, additive to either path, masks softening,
  restores no sub-pixel structure. A follow-up to whichever lands, not a substitute.
- **Supersampled history without upscaling** — history at 2× the render size even at scale 1.0. A
  variant of the same code (step 2 with output = render on present) that improves the far case
  everywhere at 4× history memory and no fill saving. Worth measuring as a switch on the same
  design.
- **Nothing** — Render Scale 1.0/2.0 for editor previews sidesteps it today; the game at native
  1080p pays it on every distant unit.

## Instruments the grill can adopt

- The far coyote under orbit at 960×540 rendered into 1920×1080: **1.05 → at or below the raw
  0.65**, judged against the 4× truth downsampled to *output* size; the near interior 0.92 → ≥ 0.95.
- Bit-identity at scale 1.0 with today's resolve, exact history readback, before any 2× figure is
  read.
- The `[resolution]` sweep at 2×; the hashed coverage ladder re-pinned at 2× (the LOD bias
  interplay is a render-size decision and should not move, but must be shown not to).

## Risks worth naming before the grill

- **A longer tail.** At 2× each output pixel takes a strong current sample only some frames, so the
  effective time constant lengthens and so does what a wrong reprojection leaves behind; the
  motion-gated weight of #372 pulls the other way. Measured, not assumed.
- **Hashed alpha's variance store** was tuned at render size; at output size the "no survivor in
  the 3×3" frame is a different event. Its instrument exists.
- **The editor's picking and screenshots** address the render grid today; both move to output.
- **Memory**: two output-res RGBA16F histories — 4× today's at 2× on Windows/Xbox alike; the roadmap
  budgets nothing for it yet.

## Explicitly not this

- Not a change to what the geometry passes render or how (render size stays theirs).
- Not the vendor upscalers (above), not sharpening, not a change to the base blend weight.
- Not the display-side stretch for scales above 1.0 — supersampling keeps working as it does.
