import idl.PbrMaterial;
import lib.forward.common;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// Loose-channel PBR material, cutout, coverage only: the receiver pass's twin of programs.forward.PBR_Loose_AlphaTest --
// the identical discard with nothing shaded, so the static-depth texture agrees with the colour
// pass texel for texel.
[shader("pixel")]
void PSMain(ForwardVSOut input)
{
    let material = LoosePbrMaterialRecord.Load(input.materialOffset);
    materialData.DiscardUncoveredAlphaTested(material, material.GetBaseColor(input.uv));
}
