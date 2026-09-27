// The pixel half of a level-of-detail dissolve: which of a dissolving placement's two levels covers a
// pixel, and the mark that tells the temporal resolve the result is stochastic coverage. Every
// opaque, cutout and hashed program calls both; a placement not dissolving pays one comparison.

import lib.forward.common;
import lib.forward.MaterialData;

uint DissolveHash(uint x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

// Uniform in [0, 1) per pixel and per frame. Screen-space and the same for both levels' draws at a
// pixel, which is what makes their coverage exact complements; a world-space hash would differ
// between two surfaces at one pixel. Moves with the hashed-alpha seed, so the resolve averages it.
public float LodDissolveHash(float2 pixel)
{
    let h = DissolveHash(uint(pixel.x) ^ DissolveHash(uint(pixel.y) ^ DissolveHash(uint(materialData.alphaHashSeed))));
    return float(h >> 8u) * (1.0 / 16777216.0);
}

public void DiscardDissolvedLod(ForwardVSOut input)
{
    if (input.lodDissolve == 1.0)
    {
        return;
    }

    let hash = LodDissolveHash(input.position.xy);
    let keeps = input.lodDissolve < 1.0 ? hash < input.lodDissolve : hash >= input.lodDissolve - 1.0;
    if (!keeps)
    {
        discard;
    }
}

// A dissolving pixel is coverage the resolve accumulates, not an opaque edge: alpha 0, as hashed
// alpha writes (docs/taa.md).
public ForwardPSOut MarkDissolvedLod(ForwardPSOut output, ForwardVSOut input)
{
    if (input.lodDissolve != 1.0)
    {
        output.color.a = 0.0;
    }
    return output;
}
