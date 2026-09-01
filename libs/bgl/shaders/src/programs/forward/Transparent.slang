import idl.MaterialType;
import lib.forward.common;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;

// The transparent colour pass, shared by both material types. Folding them into one pipeline is what
// lets the depth-sorted draw be a single dispatch, so a run in the sorted list never has to split
// just because the material type changed.
//
// The kind comes from the record's own header rather than from a varying the geometry stage stamped
// off the PSO: this is the one pass that draws more than one kind, and a reference that says what it
// names is what lets it ask. The load is uniform across the primitive.
[shader("pixel")]
float4 PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace) : SV_Target
{
    if (LoadMaterialKind(input.materialOffset) == MaterialType.kLoosePbr)
    {
        return materialData.ShadeBlended<LoosePbrMaterialRecord>(input, isFrontFace);
    }
    return materialData.ShadeBlended<PbrMaterialRecord>(input, isFrontFace);
}
