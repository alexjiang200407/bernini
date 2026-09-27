import idl.PbrMaterial;
import lib.forward.common;
import lib.forward.lod_dissolve;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// Baked triplet PBR material, stochastic coverage: base-color alpha is tested against a per-pixel
// hashed threshold rather than the material's cutoff. Opaque shape -- depth write, no blend, and a
// velocity output like any other geometry.
ForwardPSOut Program(ForwardVSOut input, bool isFrontFace)
{
    ForwardPSOut output;
    output.color = materialData.ShadeHashedAlpha<PbrMaterialRecord>(input, isFrontFace);
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
