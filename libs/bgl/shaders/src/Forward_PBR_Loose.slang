import forward.common;
import forward.MaterialData;
import idl.LoosePbrMaterial;

// unbaked, per-channel "loose" PBR material.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = Shade<LoosePbrMaterial>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
