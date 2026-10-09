import idl.LoosePbrMaterial;
import lib.forward.common;
import lib.forward.GroundColor;
import lib.forward.MaterialData;

// A terrain's albedo under an unbaked, per-channel "loose" PBR material, for the Ground Color pass.
[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    return materialData.GroundColorOf<LoosePbrMaterialRecord>(input);
}
