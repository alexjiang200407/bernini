# How to generate Environment Maps using CMFT Studio

The renderer's image-based lighting takes three files, all consumed as **linear radiance**:

| File | What it is | Sampled by |
|---|---|---|
| `pmrem.ktx2` | **P**refiltered **M**ip-mapped **R**adiance **E**nvironment **M**ap — one mip per roughness | the specular lobe (`prefilterMap`) |
| `iem.ktx2` | **I**rradiance **E**nvironment **M**ap — the cosine convolution | the diffuse term (`irradianceMap`) |
| `brdf_lut.ktx2` | split-sum BRDF lookup table (not environment-specific; generate once) | both |

`pmrem` and `iem` are two halves of **one** environment. They must be generated from the same source
in the same units, or the diffuse and specular terms disagree about how bright the world is.

---

## Gamma: set every gamma field to 1.0

CMFT exposes a **gamma before processing** and a **gamma after processing** field on *both* the
radiance and the irradiance filter. **All four must be 1.0.** This is the single easiest way to
silently ruin these maps, so it gets its own section.

The two fields exist for a source that is *not* linear:

* **Gamma before processing** linearizes a gamma-encoded input. Filtering is a weighted *average* of
  radiance and is only physically valid in linear space, so a sRGB-encoded PNG must be linearized
  first. **A `.hdr` is already linear radiance — there is nothing to undo.**
* **Gamma after processing** re-encodes the result for display or for storage in an LDR file. **Our
  output is float, consumed as linear radiance by the shader**; the engine tone maps (AgX) and
  sRGB-encodes at the very end of the frame. There is nothing to apply.

Set either one and you are distorting physical radiance values that the BRDF then treats as physical.
The failure is quiet, because the result still *looks* like a plausible environment map:

* Highlights are crushed. A gamma of 2.2 on both fields compounds to ~4.8 and took a real sun peak of
  **833 down to 7.5** — the entire HDR range that the specular lobe feeds on, gone.
* The irradiance map goes **flat**. Gamma pushes everything toward 1.0, so the contrast between the
  bright sky and the dark ground collapses (a real up/down ratio of ~6× measured as ~1.2×). Diffuse
  then barely responds to the surface normal.
* Together those produce a distinctive symptom: because the diffuse term is directionless and the
  specular term is the only view-dependent one left, **the lighting appears to follow the camera** as
  you orbit.

If the render is too bright, that is **exposure**, not gamma. Use `ISceneView::SetExposure` — it is a
tone knob and costs nothing. Reaching for gamma to dim a map destroys data.

---

## Import the source (.hdr file)
![alt text](./images/envmaps-1.png)

**Notes**
- Do not tonemap the skybox. The source must stay linear, unclamped HDR.

## Generate the Radiance Texture (`pmrem`)

![alt text](./images/envmaps-2.png)

**Options**

- Edge Fixup -> Warp
- Disable "Use OpenCL" option
- **Gamma before / after processing -> 1.0** (see above)
- Modify resolution depending on needs
- Set CPU cores depending on your Computer Specifications

**Click Process**

- Wait a while
- It may stall. If that is case Kill the process using task manager and restart

**Modify LOD**
- Modify LOD

**Click Save**

- File Type: dds
- Output Type: Cubemap
- Format RGBA32F, we compress later in Editor
- Then click **Save** again at the bottom

The shader assumes a **7-mip** chain (`MAX_REFLECTION_LOD = 6` in
[PbrShading.slang](../libs/bgl/shaders/src/forward/PbrShading.slang)): mip 0 is roughness 0, mip 6 is
roughness 1. A chain with a different mip count silently remaps roughness.

## Generate the Irradiance Texture (`iem`)

![alt text](./images/envmaps-3.png)

**Options**

- **Gamma before / after processing -> 1.0** (see above)
- Modify resolution depending on needs. 128 is good enough

**Click Process**

- Wait a while

**Click Save**

- File Type: dds
- Output Type: Cubemap
- Format RGBA32F, we compress later in Editor
- Then click **Save** again at the bottom

---

## Verify before you ship them

These maps fail *quietly* — a wrong one still renders a plausible-looking image, and the bug surfaces
much later as "the lighting looks odd". Check them. All three numbers are cheap to compute and each
catches a different mistake:

1. **`pmrem`'s mean radiance is the same at every mip.** A prefilter mip is a normalized weighted
   average of the same environment, so blurring moves energy around but cannot create it. A mean that
   *climbs* with roughness means the filter is not normalizing (the classic bug is dividing by the
   sample count instead of by `sum(NdotL)`), and the roughness-1 specular will be far too bright.
2. **That mean matches the source `.hdr`'s solid-angle-weighted mean.** If it doesn't, a gamma is on.
   Cube texels do not have equal solid angle — weight by `(1 + u² + v²)^-1.5` or the number is wrong.
3. **`iem`'s mean equals `pmrem`'s**, and its up/down face ratio is several times (not ~1×). This is
   the check that the two halves agree, and the one that catches a flattened irradiance.

For `forest.hdr` (mean 0.777, max 832.7) a correct pair measures:

```
pmrem   mean 0.781 at every mip (mip0 max ~177 -- HDR peak survives)
iem     mean 0.781, up/down 6.26x
```

## Exposure

An HDR environment's absolute scale is arbitrary, so **exposure is a property of the environment** and
must be reset whenever you change these maps. AgX places scene-linear `0.18` at middle grey, so:

> `exposure = 0.18 / L`, where `L` is the radiance an 18% grey surface reflects in the environment
> (i.e. `0.18 x iem_mean`).

For `forest.hdr` that gives `0.18 / (0.96 x 0.781 x 0.18)` ≈ **1.33**. Set it with
`ISceneView::SetExposure` (per-view — see [ISceneView.h](../libs/bgl/include/bgl/ISceneView.h)), or
pass `--exposure` to `bgl_base` to try values without a rebuild.
