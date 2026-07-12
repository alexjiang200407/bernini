import forward.common;
import forward.MaterialData;
import idl.PbrMaterial;

// Baked triplet PBR material, cutout: base-color alpha below the material's cutoff is discarded.
[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    return ShadeAlphaTested<PbrMaterial>(input);
}
