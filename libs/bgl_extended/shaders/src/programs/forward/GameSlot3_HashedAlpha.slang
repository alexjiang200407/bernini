import game.slot3;
import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.common;

// Reserved game slot 3, hashed: the surface's coverage becomes a per-pixel survival probability.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = materialData.ShadeGameHashedAlpha<Slot3Surface>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
