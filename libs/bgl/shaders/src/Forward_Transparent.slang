import forward.common;
import forward.MaterialData;
import idl.PbrMaterial;
import idl.LoosePbrMaterial;

// The transparent colour pass, shared by both material types. Folding them into one pipeline is what
// lets the depth-sorted draw be a single dispatch, so a run in the sorted list never has to split
// just because the material type changed.
[shader("pixel")]
float4 PSMain(ForwardVSOut input, bool isFrontFace: SV_IsFrontFace) : SV_Target
{
    if (input.materialIsLoose != 0u)
    {
        return ShadeBlended<LoosePbrMaterial>(input, isFrontFace);
    }
    return ShadeBlended<PbrMaterial>(input, isFrontFace);
}
