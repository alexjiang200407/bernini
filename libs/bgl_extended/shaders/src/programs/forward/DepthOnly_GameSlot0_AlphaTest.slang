import game.slot0;
import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.common;

// Reserved game slot 0, coverage only: the receiver pass's twin of programs.forward.GameSlot0_AlphaTest -- the identical
// discard with nothing shaded, so the static-depth texture agrees with the colour pass texel for
// texel.
[shader("pixel")]
void PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    materialData.DiscardUncoveredGameAlphaTested<Slot0Surface>(input, isFrontFace);
}
