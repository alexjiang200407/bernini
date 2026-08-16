# taa-skinned-vat-mesh-ghosting — implementation plan

## Context

With a VAT clip playing under a still camera in the Animation panel, the mesh trails a wake on the
background it leaves and its own edges stay jaggy and shimmer, as if nothing accumulates on it. Both
are far more visible at a 1080p sample grid (Render Scale 0.5 on a 2× display) than at 4K. The VAT
motion vectors are correct — `Forward_VatMesh.slang` re-evaluates the pose at `prevTime` through the
previous camera, `RenderContext` keeps `prevTime` per view beside the previous camera, and
`VatPlayback_test` pins the displacement — so the failure is in the resolve, not the reprojection.
(The reviewer's eyes at that render scale later found an older cause beside it, in the *sky's*
reprojection rather than the mesh's — commit 6 below; the resolve findings stand on their own
measurements.)

Read from `TaaResolve.slang`, two things a still camera and a moving mesh produce that no pan can:

- A silhouette pixel half covered by the mesh carries either the mesh's velocity or the backdrop's
  zero, by which fragment won its centre, so the edge mixture reprojects from the wrong place on
  alternate pixels and the outline doubles. Under a pan the mesh and its backdrop share the
  camera's motion, which is why the pan instruments never saw it — and why velocity dilation was
  measured on pans and rejected there.
- The variance store's resting gate is `cameraStill && own motion ≈ 0`, built so a camera pan
  cannot bank a passing edge's contrast into background pixels. The backdrop beside an animating
  mesh reports zero motion while the silhouette sweeps over it, banks that contrast, and next frame
  widens its clamp box and deepens its blend weight over exactly the wake the clamp exists to scrub.

The jaggies are the opposite failure — history *not* admitted — and are the σ-box's known trade
under motion, which at 1080p a limb crosses more texels per frame to reach.

## Decisions

- **ADR-1 — Fix it in the resolve, measured first.** The instrument comes before the change: a
  `[taa][vat][render]` case animates the VAT quad under a still camera and measures its outline
  against the same pose held, and whether its edges still resolve; the current resolve must fail
  the outline bound before any change is accepted as the fix. *Rejected: tuning by eye in the editor, because every
  prior TAA decision in `docs/taa.md` is a measured trade and an unmeasured one would be the first
  that could not be defended.*
- **ADR-2 — The trail wins over sharpness under motion.** If a mechanism trades the wake against
  the moving mesh's convergence, the wake goes. The flicker figure is still measured and must not
  regress; if the same fix improves it, good. *Rejected: reopening the σ-box decision so an
  animating surface accumulates like a still one, because that fights a measured ADR already in
  `docs/taa.md`.*
- **ADR-3 — Every existing `[taa]` figure holds, bitwise or better.** Resting stochastic coverage,
  the pan trail, the fence detail — none are up for renegotiation. *Rejected: a global blend or
  clamp retune, because those figures are what earned each mechanism.*
- **ADR-4 — The silhouette is told from the pan by object motion, not by the camera being still.**
  A tap's written vector minus what the camera alone moves a surface at its depth is zero for
  everything static, hashed or sky under any camera, and nonzero only for a mesh that animates —
  so the resolve dilates on that, and an animating mesh under a moving camera gets the same fix as
  under a still one. *Rejected: gating on `cameraStill`, the first version, because the user
  reports the same ghost under an orbit and a crowd the camera tracks is the normal case.
  Rejected: an animation flag written into the velocity buffer by the VAT path, because it widens
  the velocity format and touches every forward shader's vertex output for what a depth read
  answers in the resolve alone.*
- **ADR-5 — The jitter index is the target's frame count.** Like the history ping-pong: two
  viewports drawn by one renderer would otherwise each see every second Halton term. *Rejected:
  leaving it as a follow-up, which the first version did on an 8% edge figure — the user asked, and
  the pin is a bit-identical image with and without a second target.*

## Non-goals

- Static-mesh instances whose transform changes: `Forward_StaticMesh.slang` reprojects the *current*
  world position, so a gizmo-dragged mesh reports zero velocity and ghosts by a different route. Its
  own PR — it needs a previous transform per instance.
- The skinned tier (it does not exist yet), and hashed alpha on VAT (a VAT geom demands an opaque
  material).
- The jitter sequence itself, the base blend weight, and the resting variance widening at true
  rest.
- Depth-based disocclusion rejection: depth is read for the camera's own motion only (ADR-4), not
  to reject history, which `docs/taa.md` measured and rejected on hashed alpha.

## Acceptance

- New `[taa][vat][render]` cases in `bgl_tests`: (a) an animating VAT surface's outline measured
  against the same pose held, both under TAA, with the unresolved pair pixel-identical as the
  guard — **red on master**, under a still camera and under a drifting one; (b) the animating
  surface's edges still resolve, no worse than held; (c) a target's converged image bit-identical
  with and without a second target sharing the renderer.
- `[taa]`, `[vat]` and `[hashedalpha]` green with every pre-existing figure unchanged,
  `just test bgl` green, the shader-touching tags run under Metal validation
  (`METAL_DEVICE_WRAPPER_TYPE=1 MTL_SHADER_VALIDATION=1`). A D3D12 run is not available on this
  machine and the PR says so.
- The user's eyes on the Animation panel at Render Scale 0.5 before the PR opens.

## Commits

1. `docs(plans): plan the animating-mesh TAA outline fix` — this file.
2. `docs(skills): close the grill on the plan, not on a chat reply` — bcp-grill § 3 and its two
   callers: the consensus is written and confirmed by review of the plan where it lands. Gate:
   read-back against bcp-implement § 0 and bcp-feature § 0.
3. `refactor(bgl_tests): share the synthesized VAT textures` — `util/VatSynth.h`: the image, the
   position row and the flat normal texture out of `VatPlayback_test.cpp` and `VatRender_test.cpp`,
   plus the sliding quad, so a second suite can animate it. Gate: `just run bgl_tests -- "[vat]"`.
4. `fix(bgl): reproject an animating mesh's silhouette by the neighbour that moves on its own` —
   the two resolve changes above with the object-motion discriminator, the case, and `docs/taa.md`.
   Gate: `[taa][vat]` green with (a) red before the shader change, and the `[taa]` +
   `[hashedalpha]` figures identical to master's.
5. `fix(bgl): walk the jitter sequence per target` — the frame count on `RenderTargetBase`, the
   two uses in `RenderContext`, the case. Gate: (c) red before, bit-identical after.
6. `fix(bgl): unproject the sky's ray through the two inverses, not the inverse of the product` —
   found by the reviewer's eyes at half render scale on a 1080p panel, where the report's jaggies
   survived commits 4 and 5: a still sky reported up to half a texel of motion between jitter
   phases, so every silhouette against it was fetched off-texel each frame. One line in
   `RenderContext`, the case in `MotionVectors_test`, `docs/passes.md`. Gate: the case red before,
   green after; every `[taa]` and `[hashedalpha]` figure identical.
