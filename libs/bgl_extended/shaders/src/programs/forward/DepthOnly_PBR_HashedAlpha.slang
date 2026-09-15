import idl.PbrMaterial;
import lib.forward.common;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// Baked triplet PBR material, stochastic coverage, coverage only: the receiver pass's twin of programs.forward.PBR_HashedAlpha --
// the identical discard with nothing shaded, so the static-depth texture agrees with the colour
// pass texel for texel.
[shader("pixel")]
void PSMain(ForwardVSOut input)
{
    materialData.DiscardUncoveredHashedAlpha<PbrMaterialRecord>(input);
}
