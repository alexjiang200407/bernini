import game.slot1;
import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.common;

// Reserved game slot 1, coverage only: the receiver pass's twin of programs.forward.GameSlot1_HashedAlpha -- the identical
// discard with nothing shaded, so the static-depth texture agrees with the colour pass texel for
// texel.
[shader("pixel")]
void PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace)
{
    materialData.DiscardUncoveredGameHashedAlpha<Slot1Surface>(input, isFrontFace);
}
