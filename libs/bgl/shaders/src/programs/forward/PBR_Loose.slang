import idl.LoosePbrMaterial;
import lib.forward.common;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// unbaked, per-channel "loose" PBR material.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = materialData.Shade<LoosePbrMaterialRecord>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
