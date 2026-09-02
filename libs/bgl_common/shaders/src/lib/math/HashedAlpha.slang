// Hashed alpha testing (Wyman & McGuire, I3D 2017).
//
// A fixed cutoff makes alpha a binary test, which is what leaves a cutout surface with a jagged
// silhouette and -- where the surface self-occludes -- only one layer. Hashing the threshold per
// pixel turns alpha into stochastic coverage instead: a fragment survives with probability equal to
// its alpha, every survivor writes real depth, and the correct blend is what the ensemble averages
// to. The ensemble is over pixels and over frames, so this is only usable with temporal AA running.

float Hash1(float2 v)
{
    return frac(1.0e4 * sin(17.0 * v.x + 0.1 * v.y) * (0.1 + abs(sin(13.0 * v.y + v.x))));
}

float Hash3(float3 v)
{
    return Hash1(float2(Hash1(v.xy), v.z));
}

/**
 * How many levels coarser than its own footprint a pixel reads its local mean alpha from.
 *
 * Too close and the mean tracks the very structure being sharpened, so the steepening has nothing to
 * work against; too far and it averages over content the pixel does not cover, which drags the
 * centre off the strands and fades them.
 */
public static const float c_MinifiedAlphaMeanBias = 1.0;

/**
 * How many levels finer than the hardware's pick the alpha driving the hash is read at.
 *
 * At the footprint level a sub-texel strand is filtered flat -- there is no shape left, only energy,
 * and stochastic coverage of a flat level is dust the sharpening cannot rescue. One level finer the
 * strand still has shape, and shape is what the hash wants: alpha near 0 or 1 makes the survival
 * decision nearly deterministic, so the strand draws as a crisp stroke instead of a coin flip. The
 * aliasing the finer read brings back is exactly what the jitter walks and the accumulation
 * averages, which is the one renderer where a negative alpha bias is free. The shading colour stays
 * at the footprint level: colour aliasing is not paid for by anything.
 */
public static const float c_HashedAlphaLodBias = -1.0;

/**
 * Steepens minified alpha about the local mean, so distant coverage keeps its shape instead of its
 * energy.
 *
 * Once a strand is sub-texel at the active mip the sampled alpha is many strands averaged, and
 * stochastic coverage that honestly reproduces that mean converges to a mixture the backdrop
 * swallows. Steepening in proportion to the minification restores the contrast between strand and
 * gap, and leaves magnified alpha -- where soft self-occluding coverage is the point of hashed --
 * exactly alone.
 *
 * `mean` is the centre it steepens about, and it is the content's own local average rather than the
 * material's cutoff. A constant centre cannot be right at every level: a chain whose levels average
 * to a value above it saturates to opaque and the strands bake together into a block, and one that
 * averages below it collapses to nothing. The local mean is the fixed point of the steepening, so
 * energy is preserved wherever the result does not clip, and a level the filter has flattened to a
 * single value -- where there is no shape left to recover -- is left alone rather than pushed to one
 * side. That degenerate case is a grating's deep mips, which is what hair is.
 *
 * The minification is the smaller screen axis on purpose: a grazing card minifies along the view
 * axis at any distance, anisotropic filtering resolves that axis, and sharpening on it would
 * harden near hair.
 */
public float SharpenMinifiedAlpha(float alpha, float mean, float2 uv, float2 texDim)
{
    // How far past its honest energy distant coverage may be lifted, and the ceiling it may never
    // pass. Steepening restores contrast but cannot restore a level the filter has flattened to one
    // value -- there is no shape left in it -- and such a level is exactly where a sub-pixel strand
    // lives, so it is lifted bodily instead. Bounded on both ends: a multiple of the mean so the
    // lift scales with what is actually there, and a ceiling so however deep the chain goes a
    // grating can never reach the solid block that rescaling its mips produced.
    const float c_MaxCoverageGain = 2.0;
    const float c_MaxCoverage = 0.5;

    float2 dx = ddx(uv) * texDim;
    float2 dy = ddy(uv) * texDim;

    float texelsPerPixel = min(length(dx), length(dy));
    float sharpened = saturate((alpha - mean) * max(1.0, texelsPerPixel) + mean);

    // The lift exists only where the level is flat -- where the steepening had nothing to work
    // with -- and how far alpha sits from the mean is exactly that measure, so the gain fades out
    // with it. Ungated it fires on the aliased partials a shaped level produces and inflates the
    // strands past their area; the band is a narrow fraction of the mean because a genuinely
    // flattened level sits at the mean up to filtering residue, while a shaped one scatters well
    // outside it. The ceiling is likewise the steepened value's own coverage where shape exists:
    // capping a restored strand at c_MaxCoverage would clip exactly what the steepening
    // recovered. Magnified content has a gain of 1 and a ceiling at its own alpha.
    const float c_FlatBand = 0.25;

    float flatness = 1.0 - saturate(abs(alpha - mean) / (c_FlatBand * max(mean, 1.0e-4)));
    float gain = lerp(1.0, clamp(texelsPerPixel, 1.0, c_MaxCoverageGain), flatness);

    return min(sharpened * gain, max(sharpened, c_MaxCoverage));
}

/**
 * A per-pixel alpha threshold in (0, 1], stable on the surface rather than on the screen.
 *
 * The hash is taken on the surface at a scale chosen so one hash cell covers about one pixel: a
 * threshold that moved with the camera would resolve to noise rather than to coverage, and one that
 * did not change with distance would stop being per-pixel as the surface receded. `surfacePos` is
 * what makes it stable; its screen-space derivatives are what keep the scale right.
 *
 * World rather than object space, which the paper uses: every instance transform here is fixed for
 * its lifetime, so world position is just as stable, and `ForwardVSOut` already interpolates it. The
 * difference only shows on a non-uniformly scaled instance, whose cells stretch with it.
 *
 * Two neighbouring scales are hashed and interpolated between, rather than snapping to one. A hard
 * switch is a visible seam across the surface at the distance where the scale changes.
 *
 * `seed` decorrelates the pattern between frames. Without it every frame draws the same fragments
 * and the accumulation converges to one sample of the distribution instead of through it.
 */
public float HashedAlphaThreshold(float3 surfacePos, float seed)
{
    // The lower bound of a 2x range, since the octave snapping below rounds down to a power of two.
    // Must keep a cell sub-pixel: neighbouring pixels sharing one collapse TAA's neighbourhood clamp
    // onto the noise. See docs/taa.md.
    const float c_HashScale = 0.5;

    // How far the two screen axes may disagree about the cell size before the smaller one wins.
    // Through the octave rounding below a grazing cell spans half-to-one times this in pixels
    // across the compressed axis: at 4 that is two to four, and pixels sharing one threshold
    // collapse the TAA clamp's neighbourhood box -- measured as 7x the frame-to-frame flicker of
    // the head-on patch. At 2 the cell is one to two pixels and the flicker measures at parity
    // with head-on; the cost is single-frame grain, which is what the accumulation removes -- the
    // correlated pattern is what it cannot.
    const float c_MaxAnisotropy = 2.0;

    float dx = length(ddx(surfacePos));
    float dy = length(ddy(surfacePos));

    // A cell is isotropic in world space but the projection is not. Sizing it off max() alone is
    // right along the stretched screen axis and many pixels too wide along the compressed one, which
    // gives one threshold to a whole streak of pixels -- correlated coverage, which reads as a
    // blotch rather than as noise and defeats the ensemble the technique depends on. It is worst at
    // grazing angles, which is most of a hair card. Bounding the ratio keeps the cell near
    // pixel-sized on both axes; the cost is a finer pattern, which is noise, which is what temporal
    // AA is here to remove.
    float deriv = min(max(dx, dy), min(dx, dy) * c_MaxAnisotropy);

    // A degenerate derivative would send pixelScale to infinity and the log2 below to NaN.
    float pixelScale = 1.0 / max(c_HashScale * deriv, 1.0e-8);

    // The two powers of two either side, so the interpolation below is over one octave.
    float2 scales = float2(exp2(floor(log2(pixelScale))), exp2(ceil(log2(pixelScale))));

    float2 alpha = float2(Hash3(floor(scales.x * surfacePos) + seed), Hash3(floor(scales.y * surfacePos) + seed));

    float lerpFactor = frac(log2(pixelScale));

    float x = (1.0 - lerpFactor) * alpha.x + lerpFactor * alpha.y;
    float a = min(lerpFactor, 1.0 - lerpFactor);

    // Interpolating the two hashes directly would bias the result towards 0.5 and cost coverage at
    // the extremes. This maps the blend back onto a uniform distribution -- Wyman & McGuire's
    // three-segment inverse, which is what makes the surviving fraction equal alpha rather than
    // merely correlate with it.
    float3 cases = float3(x * x / (2.0 * a * (1.0 - a)), (x - 0.5 * a) / (1.0 - a), 1.0 - ((1.0 - x) * (1.0 - x) / (2.0 * a * (1.0 - a))));

    float threshold = (x < (1.0 - a)) ? ((x < a) ? cases.x : cases.y) : cases.z;

    // Never 0 or 1: at 0 a fully transparent fragment would survive, and at 1 a fully opaque one
    // would be discarded.
    return clamp(threshold, 1.0e-6, 1.0);
}
