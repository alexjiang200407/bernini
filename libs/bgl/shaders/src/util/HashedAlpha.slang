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
    // How much a hash cell may be magnified before the next scale is picked up. 1 keeps a cell at
    // roughly a pixel; larger trades noise for a coarser, blotchier pattern.
    const float c_HashScale = 1.0;

    // Pixels per hash cell, from the fastest-changing screen axis of the object position.
    float pixelScale = 1.0 / (c_HashScale * max(length(ddx(surfacePos)), length(ddy(surfacePos))));

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
