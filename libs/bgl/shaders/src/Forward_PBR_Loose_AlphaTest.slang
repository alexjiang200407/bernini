import forward.common;
import forward.MaterialData;
import idl.LoosePbrMaterial;

// Unbaked, per-channel "loose" PBR material, cutout. A loose material routes its alpha explicitly
// (baseColor[3]), so it can always sample a real alpha channel -- see ShadeAlphaTested.
[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    return ShadeAlphaTested<LoosePbrMaterial>(input);
}
