import game.slot0;
import game.slot1;
import game.slot2;
import game.slot3;
import idl.MaterialType;
import lib.forward.GameSurface;
import lib.forward.common;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// The transparent colour pass, shared by every material type. Folding them into one pipeline is
// what lets the depth-sorted draw be a single dispatch, so a run in the sorted list never has to
// split just because the material type changed.
//
// The kind comes from the record's own header rather than from a varying the geometry stage stamped
// off the PSO: this is the one pass that draws more than one kind, and a reference that says what it
// names is what lets it ask. The load is uniform across the primitive.
[shader("pixel")]
float4 PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace) : SV_Target
{
    switch (LoadMaterialKind(input.materialOffset))
    {
    case MaterialType.kLoosePbr:
        return materialData.ShadeBlended<LoosePbrMaterialRecord>(input, isFrontFace);
    case MaterialType.kGame0:
        return materialData.ShadeGameBlended<Slot0Surface>(input, isFrontFace);
    case MaterialType.kGame1:
        return materialData.ShadeGameBlended<Slot1Surface>(input, isFrontFace);
    case MaterialType.kGame2:
        return materialData.ShadeGameBlended<Slot2Surface>(input, isFrontFace);
    case MaterialType.kGame3:
        return materialData.ShadeGameBlended<Slot3Surface>(input, isFrontFace);
    default:
        return materialData.ShadeBlended<PbrMaterialRecord>(input, isFrontFace);
    }
}
