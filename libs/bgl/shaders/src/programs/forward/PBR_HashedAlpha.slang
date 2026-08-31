import idl.PbrMaterial;
import lib.forward.common;
import lib.forward.MaterialData;

// Baked triplet PBR material, stochastic coverage: base-color alpha is tested against a per-pixel
// hashed threshold rather than the material's cutoff. Opaque shape -- depth write, no blend, and a
// velocity output like any other geometry.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = ShadeHashedAlpha<PbrMaterialRecord>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
