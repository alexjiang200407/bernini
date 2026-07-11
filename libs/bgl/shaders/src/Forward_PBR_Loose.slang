import forward.common;
import forward.MaterialData;
import idl.LoosePbrMaterial;

// unbaked, per-channel "loose" PBR material.
[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    return Shade<LoosePbrMaterial>(input);
}
