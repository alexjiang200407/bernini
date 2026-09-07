import game.slot0;
import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.common;

// Reserved game slot 0, opaque: whatever surface game.slot0 binds, lit by the PBR model.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = materialData.ShadeGame<Slot0Surface>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
