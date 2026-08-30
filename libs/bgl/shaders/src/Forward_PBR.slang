import forward.common;
import forward.MaterialData;
import idl.PbrMaterial;

// baked triplet PBR material.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = Shade<PbrMaterialRecord>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
