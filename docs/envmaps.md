# How to generate Environment Maps using CMFT Studio

The renderer's image-based lighting takes four files, all consumed as **linear radiance**:

| File | What it is | Resolution | Sampled by |
|---|---|---|---|
| `pmrem.ktx2` | **P**refiltered **M**ip-mapped **R**adiance **E**nvironment **M**ap — one mip per roughness | 256², 7 mips | the specular lobe (`prefilterMap`) |
| `iem.ktx2` | **I**rradiance **E**nvironment **M**ap — the cosine convolution | 128², 1 mip | the diffuse term (`irradianceMap`) |
| `skybox.ktx2` | the environment itself, drawn as the background | 512², 1 mip | `SkyboxPass` (`cubeTex`) |
| `brdf_lut.ktx2` | split-sum BRDF lookup table (not environment-specific; generate once) | 256², `RG16_SFLOAT` | both |

`pmrem` and `skybox` are deliberately **separate files**, because the two roles want opposite things:
the prefilter is sampled through a roughness lobe and 256² is already generous, while the skybox is
seen directly at viewport resolution. Pointing `skybox` at `pmrem` costs 16× the prefilter's memory to
buy sharpness only the background can use.

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

- **Edge Fixup -> None** (see below — `Warp` double-corrects and creases the seams)
- Disable "Use OpenCL" option
- **Gamma before / after processing -> 1.0** (see above)
- Set the resolution to what you intend to ship; do not resize afterwards (see below)
- Set CPU cores depending on your Computer Specifications

**Click Process**

- Wait a while
- It may stall. If that is case Kill the process using task manager and restart

**Modify LOD**
- Modify LOD

**Click Save**

- File Type: **ktx** -- `ktx` reads it and DDS it does not; the conversion below starts there
- Output Type: Cubemap
- Format RGBA32F -- an intermediate, converted to RGB9E5 below
- Then click **Save** again at the bottom

The shader assumes a **7-mip** chain (`MAX_REFLECTION_LOD = 6` in
[PbrShading.slang](../libs/bgl/shaders/src/forward/PbrShading.slang)): mip 0 is roughness 0, mip 6 is
roughness 1. A chain with a different mip count silently remaps roughness.

### Edge Fixup must be None

CMFT's `Warp` fixup stretches each face's texel centres outward — `u' = a·u³ + u` with
`a = n²/(n-1)³` — so the outermost one lands exactly on the face edge. That is a correction for
hardware that cannot filter across a cube seam. **D3D12 and WebGPU both do it in hardware**, always
on, so `Warp` gets applied twice: content near a border ends up displaced by up to half a texel, in
opposite directions on the two faces sharing it, and the seam shows as a bright crease.

The displacement is a fraction of a *texel*, so its angular size scales with the mip — about 0.04° at
1024² but 2.8° at the 16² roughness-1 mip. That is why it appears as "seams only at roughness 1"
rather than as a uniform problem, and why it is easy to live with for a long time before diagnosing.

A map that has it is recognisable without the source: its border texels are **bitwise identical** to
their partners across every seam, which is only possible if both faces sample the exact same
direction. A correctly generated map has them roughly one texel apart, like any other neighbours.

## Generate the Irradiance Texture (`iem`)

![alt text](./images/envmaps-3.png)

**Options**

- **Gamma before / after processing -> 1.0** (see above)
- Modify resolution depending on needs. 128 is good enough

**Click Process**

- Wait a while

**Click Save**

- File Type: **ktx** -- `ktx` reads it and DDS it does not; the conversion below starts there
- Output Type: Cubemap
- Format RGBA32F -- an intermediate, converted to RGB9E5 below
- Then click **Save** again at the bottom

## Generate the Skybox (`skybox.ktx2`)

The background is the environment itself, **unfiltered** — so this one runs no filter at all. Import
the `.hdr`, set the output face size, and save; do not press Process.

**Options**

- Output Type: Cubemap, face size 512
- **Edge Fixup -> None**, as above
- No tonemapping, no gamma — the sky is linear HDR like everything else here

Only one mip is needed: `SkyboxPass` samples an explicit `SkyboxDesc::mipLevel`, which defaults to 0.

---

## Convert to RGB9E5

CMFT writes `RGBA32F`, which is 16 bytes a texel and far more than radiance needs. Ship
`E5B9G9R9_UFLOAT_PACK32` instead: 4 bytes a texel, a 5-bit exponent shared across a 9-bit mantissa per
channel. It is filterable everywhere without an optional feature — WebGPU core `rgb9e5ufloat`, D3D12
`R9G9B9E5_SHAREDEXP`, Metal `RGB9E5Float` — which is why it is preferred over BC6H, whose 1 byte a
texel is unreachable on Apple GPUs. `R11G11B10` is the same size but bands in sky gradients, its blue
channel carrying only 5 mantissa bits.

**Generate at the resolution you intend to ship** — set it in CMFT, in the "Modify resolution" field,
and let the conversion below change only the format. Then `ktx` never resamples, and the step that
follows is spatially neutral:

```bash
ktx2ktx2 pmrem_cmft.ktx                  # CMFT writes KTX1; everything below needs KTX2
ktx extract --all pmrem_cmft.ktx2 ex/    # names them <prefix>_level<L>_face<F>.exr,
                                         # which is also the order ktx create wants
FILES=$(for L in 0 1 2 3 4 5 6; do for F in 0 1 2 3 4 5; do echo ex/output_level${L}_face${F}.exr; done; done)

ktx create --format E5B9G9R9_UFLOAT_PACK32 --cubemap --levels 7 \
           --assign-tf linear --assign-primaries bt709 $FILES pmrem.ktx2
ktx deflate --zstd 19 pmrem.ktx2 pmrem.ktx2
```

`iem.ktx2` and `skybox.ktx2` are the same, with `--levels 1` and the six `output_face<F>.exr` that a
single-level `extract --all` produces. The zstd pass is worth taking: it is transparent to the loader
(`ktxTexture2_CreateFromNamedFile` inflates it) and RGB9E5 compresses far better than float32, whose
low mantissa bits are incompressible noise.

Two things that look like they would work and do not:

* **Do not resize with `ktx create --width/--height`.** Its resampling kernel is wider than the
  output texel, so at a face border it reaches past the edge and clamps — and the two faces sharing
  that edge clamp to different data. A correct cube map has each border texel *equal* to its partner
  across the seam (the CMFT source measures 0.00% mismatch at every mip); resizing 1024 → 256 this way
  took that to **32% mean, 178% peak**, which reads on a mirror surface as bright lines tracing the
  cube's edges and corners. Downsampling a cube face at all needs an exact box average (never reaching
  past the border) *followed by* averaging each matched edge pair and each 3-way corner — which is
  what CMFT's own edge fixup does, and why generating at the target resolution is the easy path.
* **Do not regenerate the mip chain with `--generate-mipmap`.** Each `pmrem` mip is a
  roughness-specific convolution, not a box filter of the one above it; a mipmap generator would
  silently replace the prefilter with a blur.

Seam continuity is worth checking whenever these files are rebuilt, because it fails exactly the way
everything else here does — quietly, and only on the shiniest material in the scene.

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
