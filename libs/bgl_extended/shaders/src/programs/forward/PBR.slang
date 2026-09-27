import idl.PbrMaterial;
import lib.forward.common;
import lib.forward.lod_dissolve;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// baked triplet PBR material.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    DiscardDissolvedLod(input);

    ForwardPSOut output;
    output.color = materialData.Shade<PbrMaterialRecord>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return MarkDissolvedLod(output, input);
}
