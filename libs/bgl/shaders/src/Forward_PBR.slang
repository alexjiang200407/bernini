import forward.common;
import forward.MaterialData;
import idl.PbrMaterial;

// baked triplet PBR material.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input)
{
    ForwardPSOut output;
    output.color = Shade<PbrMaterial>(input);
    output.motion = ComputeMotionVector(input);
    return output;
}
