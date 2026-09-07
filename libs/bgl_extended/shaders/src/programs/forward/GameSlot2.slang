import game.slot2;
import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.common;

// Reserved game slot 2, opaque: whatever surface game.slot2 binds, lit by the PBR model.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = materialData.ShadeGame<Slot2Surface>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
