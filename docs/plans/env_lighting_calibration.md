# Environment lighting calibration — implementation plan

Bernini renders a model brighter and much flatter than Blender's Material Preview does under the
same kind of environment. This plan records what was measured, what was ruled out, and what to do.
The goal is **close enough that a model authored in Blender reads the same here** — not 1:1.

This is a *plan*, not a mirror of code. When the work lands, the durable parts belong in
[envmaps.md](../envmaps.md) (the exposure and orientation contracts) and [passes.md](../passes.md);
this file keeps the reasoning and the measurements.

---

## 1. What was measured

Numbers come from decoding the shipped `assets/` KTX2 containers directly, from evaluating `AgX` on
paper, and from photometric analysis of the reference screenshots. All are reproducible without
building the engine.

### 1.1 The tone map is correct — not a suspect

`AgX` in [util/Tonemap.slang](../../libs/bgl/shaders/src/util/Tonemap.slang) maps scene-linear
`0.18` to **0.5005** in sRGB display code. Blender's AgX places middle grey at 0.5 by construction.
They agree to within a fifth of a percent.

The inset/outset matrices are transposed correctly for Slang's row-vector `mul`, and the closing
`pow(color, 2.2)` (line 47) does linearize the display-encoded result, so the sRGB backbuffer does
not double-encode. The commonest way to get a washed-out AgX is not present.

### 1.2 The baked exposure is exactly what the formula asks for

`assets/EnvLighting/forest.benvl` stores `exposure = 1.3388171`. Decoding
`Textures/forest_irradiance.ktx2` and repeating `exposureFor`'s solid-angle-weighted mean gives
`0.778048`, and `1 / (0.96 × 0.778048) = 1.3388` — exact.
[envmap_bake.cpp:692-726](../../libs/assetlib/src/envmap_bake.cpp) has no arithmetic bug.

Against Blender, which uses an HDRI at strength 1.0 with no normalization, that is **+0.42 EV** —
about `+0.054` of display code at middle grey. Real, but small.

`exposureFor` averages `(r+g+b)/3` rather than luminance. Here that is worth **0.015 EV** (mean
irradiance is near-neutral at `(0.754, 0.771, 0.809)`), so it is a correctness nit, not a cause.

### 1.3 The environment is strongly directional; the render is not

**This is the main finding, and it is the one worth acting on first.**

The two reference screenshots are the same model from opposite sides. Fitting each render's
*background* against `forest_sky.ktx2` — searching camera yaw, pitch and FOV — locks on tightly:

| view | correlation | recovered camera forward |
|---|---|---|
| front | **0.9901** | `(+0.98, −0.10, +0.16)`, yaw 81° |
| back | **0.9888** | `(−1.00, +0.07, −0.05)`, yaw 267° |

So the camera genuinely orbited ~186° relative to the environment, and **the skybox is drawn with
the correct orientation** — a 0.99 correlation against the shipped cube leaves no room for a flipped
backdrop.

The irradiance cube is strongly directional: sampled over the sphere it ranges from **0.084 to
1.978, a 4.56 EV swing**. Cosine-weighting it over each view's *camera-visible hemisphere*:

| | visible-hemisphere mean irradiance |
|---|---|
| front view | 0.4645 |
| back view | 1.0309 |

So a correctly lit model **must appear 1.15 EV darker in the front view than in the back view**.

Measured off albedo-matched body parts (ears, hind legs, head — avoiding the light chest fur), the
model is **0.23 EV *brighter*** in the front view. The best-matched parts alone (right ear, hind
legs) give −0.25 and −0.21 EV where −1.15 EV is required.

**The render is roughly 1.4 EV flatter than the environment says it should be.** A model lit by a
4.6 EV directional environment barely changes as the camera orbits to the opposite side. That is a
lookup-direction problem, and no exposure or tone-map change will fix it.

It is **not** a clean 180° flip: that would predict the front view being *2.10 EV brighter*, and
0.23 EV is nowhere near it. Something is partially decorrelating the IBL lookup from world space,
not reversing it.

### 1.4 The skybox blur lifts the backdrop's shadows ~1.9 EV

The shipped sky was baked with `--skybox-blur 0.15`, destructively — `forest_sky.ktx2` is a
single-mip 256² cube that is *already* convolved. Against the sharp projection
(`forest_prefilter.ktx2` mip 0, same size, same source):

| | scene mean | scene p1 | scene max | display p1 | display p5 | display mean |
|---|---|---|---|---|---|---|
| sharp | 0.824 | 0.014 | 1094 | 0.102 | 0.199 | 0.512 |
| blurred (drawn) | 0.823 | 0.052 | 93 | 0.281 | 0.328 | 0.568 |

Energy is conserved exactly as the prefilter tests require — the means are identical — but the blur
smears bright sky across dark foliage, so the **1st percentile rises 0.014 → 0.052 scene-linear,
+1.9 EV**. Measured off the screenshots, our backdrop sits **+0.56 EV** above Blender's. Since the
skybox is drawn at exposure 1.0 (§1.5), *none* of that 0.56 EV is the exposure — it is the blur.

Not a bug; it is the documented material-editor aesthetic. It is still why the background does not
match.

### 1.5 Sky and geometry are exposed differently

Every call site passes a literal `1.0f` for `SkyboxDesc::exposure` while handing the view the
environment's derived exposure:
[environment.cpp:38 vs :64](../../apps/editor/src/Render/environment.cpp),
[bgl_base/main.cpp:125 vs :129](../../examples/bgl_base/src/main.cpp),
[bgl_sphere/main.cpp:80 vs :84](../../examples/bgl_sphere/src/main.cpp), and the usage sketch in
[envmaps.md](../envmaps.md).

The backdrop is therefore 0.42 EV darker *relative to* the objects than the shared environment says.
Small today; a correctness trap the moment anyone changes exposure, because the sky would not
follow.

### 1.6 Missing occlusion is *not* the main gap

An earlier draft of this document blamed unoccluded IBL. That was wrong, and the correction matters:
**Blender's Material Preview is Eevee with a studio HDRI — no lamps, no shadow casters, and ambient
occlusion off by default.** It is unoccluded too. Comparing an unoccluded renderer against another
unoccluded renderer, the absence of AO cannot be the difference.

Consistent with that, the subject-to-backdrop tonal *relationship* is similar in both images —
within roughly 0.0–0.3 EV across the tonal range. (Indicative only: our subject mask is contaminated
because the model overlaps the backdrop's tonal range, which is itself a symptom of §1.3.)

Bernini genuinely has no occlusion — the pass list is `BrdfLutGen`, `Clear`, `CompactInstances`,
`Forward`, `PostProcess`, `PreparePresent`, `Skybox`, `TaaResolve`, `TransparentSort`; there are no
analytic lights; `ao` is the ORM red channel, which is `0xFF` for a glTF with no occlusion map
([material_bake.cpp:40](../../libs/assetlib/src/material_bake.cpp)) and a white fallback texture for
a material with no ORM ([Scene.cpp:955](../../libs/bgl/src/scene/Scene.cpp)). `ROADMAP.md:257-264`
and `:297` already want lights, shadows and AO. All true, all worth building — just not the
explanation for *this* comparison.

### 1.7 Ruled out, with evidence

Recorded so nobody re-opens them:

- **A flip or rotation introduced by the bake.** The luminance centroids of the sky, prefilter and
  irradiance cubes agree to **0.03°**. `faceTexelDir`
  ([envmap_bake.cpp:20-40](../../libs/assetlib/src/envmap_bake.cpp)) is exactly the standard
  D3D/GL cube face convention, and the r=0.99 background fit in §1.3 confirms it end to end.
- **View-space shading normals** (which would make shading view-invariant). Normals are transformed
  to world space at [Forward_StaticMesh.slang:46](../../libs/bgl/shaders/src/Forward_StaticMesh.slang).
- **CPU skinning silently zeroing normals.** It blends them through the same matrices
  (`skinning.cpp:128`), and `decodableOffset` *throws* on an unexpected format rather than returning
  "absent", so a packed normal cannot become a silent zero.
- **Base-colour colour space.** Baked base colours are `BC7_SRGB` / `BC1_RGB_SRGB`; ORM is
  `BC7_UNORM`, normals `BC5_UNORM`. Correct.
- **The `1/pi` convention on the irradiance map.** Test-pinned, and the shipped maps agree: mean
  irradiance 0.778 against mean prefilter radiance 0.824.
- **Double gamma after the tone map**, and **auto-exposure drift** (there is none in the repo).

---

## 2. Root causes, ranked

1. **The IBL lookup does not track world space the way the skybox does** (§1.3) — ~1.4 EV of missing
   directionality. Dominates, and it is why the model reads flat and washed out from the front and
   "more correct" from behind.
2. **The backdrop is baked pre-blurred** (§1.4), lifting its darkest percentiles ~1.9 EV and putting
   our background +0.56 EV above Blender's.
3. **Every environment is renormalized to middle grey** (§1.2), +0.42 EV here. Small, but
   structural: while `exposureFor`'s output is used unconditionally, no environment can match a
   reference renderer, because dim and bright environments are forced to the same average.
4. **Sky and geometry disagree about exposure** (§1.5). Cosmetic today, a trap after (3).

---

## 3. The plan

### T0 — Find and fix the IBL orientation defect

Everything else is cosmetic until this is settled, and it is cheap to settle.

**The diagnostic already exists.** `MaterialPreviewWindow::ShowDefaultSphere`
([MaterialPreviewWindow.cpp:173-198](../../apps/editor/src/Windows/MaterialEditor/MaterialPreviewWindow.cpp))
puts up a sphere with `baseColorFactor = 1`, `roughness = 1`, `metallic = 0` — a matte white ball,
which is precisely the probe this needs. Orbit it against the `forest` backdrop. In an environment
with a 4.56 EV directional swing, that sphere **must** have an obvious bright side that stays locked
to the world as the camera moves. If it is flat, or its bright side follows the camera, the lookup
direction is wrong.

Then bisect the direction's path, in this order — each is a one-line check:

- The world normal reaching `ShadeSurface`, against the same vertex's normal in the mesh. Confirm
  the instance transform is what the shader thinks it is, including for a *skinned* instance, which
  is the path the reference asset (`coyote_skinned.bmesh`) takes and the one this branch is
  changing.
- `R = reflect(-V, N)` and the `SampleCube` argument, against the skybox's `clipToWorld` ray for the
  same pixel. Both must be world-space directions in the same handedness. **They are reconstructed
  by different code with no shared test**, which is exactly how a mirror survives.
- `skyRotation`. [RenderContext.cpp:514-537](../../libs/bgl/src/gfx/RenderContext.cpp) applies
  `BSky::rotationY` to the skybox's `clipToWorld`, but `PbrShading` samples the IBL cubes with a raw
  world normal and no corresponding rotation. `forest` has `rotationY = 0`, so this is not today's
  bug — but it *is* a latent one: any non-zero `rotationY` rotates the backdrop away from the
  lighting. Fix it alongside, by rotating the IBL lookup or by rejecting a rotation the IBL cannot
  honour.

Lock the result down with a test: a matte white sphere in a synthetic environment that is bright in
one hemisphere and black in the other, asserting the bright side of the render lands on the bright
side of the environment. That is a golden image that fails loudly on a mirror, a swapped axis, or a
rotation — none of which the current suite can catch, since every existing environment test checks
the *bake* and the bake is correct.

### T1 — One exposure for the whole view

Fold `draw.lighting.exposure` into the value `SkyboxPass` writes into `gSkyboxData.exposure`, so
`SkyboxDesc::exposure` keeps its meaning as an *additional* per-sky gain and every call site's
`1.0f` becomes "no extra gain" rather than "ignore the environment". Fixes the editor, both
examples and any future caller in one place; no call site changes. Update the usage sketch in
[envmaps.md](../envmaps.md) and the `SkyboxDesc::exposure` doc comment.

### T2 — Make the environment's exposure authorable, and ship the default at Blender parity

`exposureFor` should propose, not decide.

- Keep the derivation and keep writing it into the `.benvl` at import — a good starting point for an
  HDRI of unknown scale.
- Add an authored override beside it, so a re-import does not discard a tuned value. The `.benvl` is
  versioned and its minor version is additive, so this is a minor bump and existing files stay
  readable.
- Surface it in the editor. Today the only control is an undocumented `exposure` key in
  `config.json` ([MainWindow.cpp:134](../../apps/editor/src/MainWindow.cpp)), absent from
  `config.example.json`.
- Re-author `assets/EnvLighting/forest.benvl` toward 1.0, which is what Blender does at world
  strength 1.0.

While the mean is being touched, switch it from `(r+g+b)/3` to Rec.709 luminance — 0.015 EV here,
but correct for a strongly tinted environment where it would not be.

> **Check first:** confirm the source HDRI behind `forest` is the image Blender is being compared
> against — the reference backdrop looks like Blender's own `forest.exr` studio light. If they are
> different images, the parity target has to be re-derived.

### T3 — Defocus the backdrop at draw time, not at bake time

The blur is a presentation choice and should not be burned into pixels.

- Bake the skybox **sharp**, with a full mip chain (it is single-mip today, which is why
  `BSky::mipLevel` is 0 and can do nothing).
- Let `BSky::mipLevel` — already plumbed through `SkyboxDesc` and already sampled by
  [Skybox.slang:66](../../libs/bgl/shaders/src/Skybox.slang) — carry the defocus, making it a
  container edit rather than minutes of convolution.
- Keep a defocused default for the *material preview*, where §1.4's reasoning is sound; default a
  level viewport to mip 0.

### T4 — Occlude the ambient term

Not the cause of this mismatch (§1.6), but the roadmap wants it and it is what will make Bernini
look better than Blender's Material Preview rather than merely equal to it. Add GTAO — a horizon
integral gives a visibility term with a defensible relationship to the cosine-weighted irradiance
the IBL applies. Consume depth and the shading normal; no new geometry work. Apply it to the
**diffuse** term and derive a separate specular occlusion rather than scaling both by one scalar.

### T5 — Give the level editor a default environment

`LevelEditorWindow` is a two-line constructor that binds no environment at all — the level viewport
has neither IBL nor sky; only the material preview and the thumbnail cache call
`editor::ApplyEnvironment`. Whatever the default look is decided to be, this is where a user meets
it.

---

## 4. Verifying

The golden images in `assets/golden/` are exposure- and orientation-sensitive, so T0, T2 and T3 each
invalidate some. Re-baseline one task at a time and look at every diff — a golden update is the only
place a regression hides silently here.

Worth adding as it goes:

- The hemisphere-probe golden image from T0. **The suite's blind spot is that every environment test
  checks the bake, and the bake is correct** — nothing tests that a world direction in the shader
  reaches the texel the bake put there.
- A calibration test asserting `AgX(0.18) == 0.5` in display code, so §1.1 cannot drift.
- A test that a bright and a dim version of one environment yield *different* exposures once T2
  lands.
- Coverage for the sharp-skybox bake and mip chain T3 introduces.

For the qualitative check, the comparison that started this: one model, one HDRI, our viewport
against Blender's with AgX and world strength 1.0. Expect the model's *form* to come back after T0,
the backdrop after T3, and the overall level after T2.
