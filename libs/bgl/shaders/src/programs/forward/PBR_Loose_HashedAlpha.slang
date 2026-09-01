import idl.LoosePbrMaterial;
import lib.forward.common;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// The per-channel material's stochastic-coverage pixel shader; see PBR_HashedAlpha.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = materialData.ShadeHashedAlpha<LoosePbrMaterialRecord>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
