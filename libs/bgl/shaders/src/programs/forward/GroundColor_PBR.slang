import idl.PbrMaterial;
import lib.forward.common;
import lib.forward.GroundColor;
import lib.forward.MaterialData;

// A terrain's albedo under a baked triplet PBR material, for the Ground Color pass.
[shader("pixel")]
GroundColorOut PSMain(ForwardVSOut input)
{
    return materialData.GroundColorOf<PbrMaterialRecord>(input);
}
