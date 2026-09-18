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
//
// This file draws the engine's own kinds. A program has to name a surface's type, so RegisterSurfaces
// (src/gfx/surface_registry.cpp) shadows it with a module of the same shape that adds one arm per
// registered surface ahead of this fallthrough.
[shader("pixel")]
float4 PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace) : SV_Target
{
    return materialData.ShadeBlendedEngineKind(input, isFrontFace);
}
