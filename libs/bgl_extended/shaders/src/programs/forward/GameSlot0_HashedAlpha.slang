import game.slot0;
import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.common;

// Reserved game slot 0, hashed: the surface's coverage becomes a per-pixel survival probability.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = materialData.ShadeGameHashedAlpha<Slot0Surface>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
