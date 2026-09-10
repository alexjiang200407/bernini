import game.slot2;
import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.common;

// Reserved game slot 2, cutout: the surface's coverage below the material's cutoff is discarded.
[shader("pixel")]
ForwardPSOut PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    ForwardPSOut output;
    output.color = materialData.ShadeGameAlphaTested<Slot2Surface>(input, isFrontFace);
    output.motion = ComputeMotionVector(input);
    return output;
}
