import forward.common;
import forward.MaterialData;
import idl.PbrMaterial;

// Baked triplet PBR material, cutout: base-color alpha below the material's cutoff is discarded.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = ShadeAlphaTested<PbrMaterial>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
