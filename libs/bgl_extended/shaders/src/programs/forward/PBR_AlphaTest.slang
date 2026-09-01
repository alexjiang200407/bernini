import idl.PbrMaterial;
import lib.forward.common;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// Baked triplet PBR material, cutout: base-color alpha below the material's cutoff is discarded.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = materialData.ShadeAlphaTested<PbrMaterialRecord>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
