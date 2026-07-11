import forward.common;
import forward.MaterialData;
import idl.PbrMaterial;

// baked triplet PBR material.
[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    return Shade<PbrMaterial>(input);
}
