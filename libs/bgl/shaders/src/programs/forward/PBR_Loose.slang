import idl.LoosePbrMaterial;
import lib.forward.common;
import lib.forward.MaterialData;

// unbaked, per-channel "loose" PBR material.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = Shade<LoosePbrMaterialRecord>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
