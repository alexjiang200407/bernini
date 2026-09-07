import game.slot3;
import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.common;

// Reserved game slot 3, opaque: whatever surface game.slot3 binds, lit by the PBR model.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = materialData.ShadeGame<Slot3Surface>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
