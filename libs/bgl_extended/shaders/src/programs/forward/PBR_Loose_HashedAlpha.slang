import idl.LoosePbrMaterial;
import lib.forward.common;
import lib.forward.lod_dissolve;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// The per-channel material's stochastic-coverage pixel shader; see PBR_HashedAlpha.
ForwardPSOut Program(ForwardVSOut input, bool isFrontFace)
{
    ForwardPSOut output;
    output.color = materialData.ShadeHashedAlpha<LoosePbrMaterialRecord>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}

[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    return Program(input, isFrontFace);
}

// A bucket's dissolve lane: the same surface, dithered against the placement's other level.
[shader("pixel")]
ForwardPSOut PSDissolve(DissolveVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    DiscardDissolvedLod(input);
    return MarkDissolvedLod(Program(input.Surface(), isFrontFace));
}
