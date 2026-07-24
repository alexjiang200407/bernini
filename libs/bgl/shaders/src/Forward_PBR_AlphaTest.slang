import forward.common;
import forward.MaterialData;
import idl.PbrMaterial;

// Baked triplet PBR material, cutout: base-color alpha below the material's cutoff is discarded.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input)
{
    ForwardPSOut output;
    output.color = ShadeAlphaTested<PbrMaterial>(input);
    output.motion = ComputeMotionVector(input);
    return output;
}
