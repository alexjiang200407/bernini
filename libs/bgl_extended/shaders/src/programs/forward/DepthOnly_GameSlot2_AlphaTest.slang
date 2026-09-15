import game.slot2;
import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.common;

// Reserved game slot 2, coverage only: the receiver pass's twin of programs.forward.GameSlot2_AlphaTest -- the identical
// discard with nothing shaded, so the static-depth texture agrees with the colour pass texel for
// texel.
[shader("pixel")]
void PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    materialData.DiscardUncoveredGameAlphaTested<Slot2Surface>(input, isFrontFace);
}
