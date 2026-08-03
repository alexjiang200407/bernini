import forward.common;
import forward.MaterialData;
import idl.LoosePbrMaterial;

// The per-channel material's stochastic-coverage pixel shader; see Forward_PBR_HashedAlpha.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input)
{
    ForwardPSOut output;
    output.color = ShadeHashedAlpha<LoosePbrMaterial>(input);
    output.motion = ComputeMotionVector(input);
    return output;
}
