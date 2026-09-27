import idl.LoosePbrMaterial;
import lib.forward.common;
import lib.forward.lod_dissolve;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// unbaked, per-channel "loose" PBR material.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    DiscardDissolvedLod(input);

    ForwardPSOut output;
    output.color = materialData.Shade<LoosePbrMaterialRecord>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return MarkDissolvedLod(output, input);
}
