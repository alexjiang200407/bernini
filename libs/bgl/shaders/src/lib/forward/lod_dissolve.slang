// The pixel half of a level-of-detail dissolve: which of a dissolving placement's two levels covers a
// pixel, and the mark that tells the temporal resolve the result is stochastic coverage. Every
// opaque, cutout and hashed program's PSDissolve entry calls both around the shading its PSMain
// does; only a bucket's dissolve lane draws through it, so a placement at rest pays nothing.

import lib.forward.common;
import lib.forward.MaterialData;
import lib.math.hash;

// Uniform in [0, 1) per pixel and per frame. Screen-space and the same for both levels' draws at a
// pixel, which is what makes their coverage exact complements; a world-space hash would differ
// between two surfaces at one pixel. Moves with the hashed-alpha seed, so the resolve averages it.
public float LodDissolveHash(float2 pixel)
{
    return HashFloat(uint(pixel.x) ^ HashUint(uint(pixel.y) ^ HashUint(uint(materialData.alphaHashSeed))));
}

public void DiscardDissolvedLod(DissolveVSOut input)
{
    let hash = LodDissolveHash(input.position.xy);
    let keeps = input.lodDissolve < 1.0 ? hash < input.lodDissolve : hash >= input.lodDissolve - 1.0;
    if (!keeps)
    {
        discard;
    }
}

// A dissolving pixel is coverage the resolve accumulates, not an opaque edge: alpha 0, as hashed
// alpha writes (docs/taa.md).
public ForwardPSOut MarkDissolvedLod(ForwardPSOut output)
{
    output.color.a = 0.0;
    return output;
}
