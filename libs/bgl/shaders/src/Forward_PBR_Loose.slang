import forward.common;
import forward.MaterialData;
import idl.LoosePbrMaterial;

// unbaked, per-channel "loose" PBR material.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input)
{
    ForwardPSOut output;
    output.color = Shade<LoosePbrMaterial>(input);
    output.motion = ComputeMotionVector(input);
    return output;
}
