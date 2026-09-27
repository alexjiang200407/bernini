import idl.LoosePbrMaterial;
import lib.forward.common;
import lib.forward.lod_dissolve;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// Unbaked, per-channel "loose" PBR material, cutout. A loose material routes its alpha explicitly
// (baseColor[3]), so it can always sample a real alpha channel -- see ShadeAlphaTested.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    DiscardDissolvedLod(input);

    ForwardPSOut output;
    output.color = materialData.ShadeAlphaTested<LoosePbrMaterialRecord>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return MarkDissolvedLod(output, input);
}
