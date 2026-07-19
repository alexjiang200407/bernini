import forward.common;
import forward.MaterialData;
import idl.PbrMaterial;
import idl.LoosePbrMaterial;

// The transparent colour pass, shared by both material types. Folding them into one pipeline is what
// lets the depth-sorted draw collapse to two PSOs (blend vs self-occluding blend), so a run in the
// sorted list never has to split just because the material type changed.
[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    if (input.materialIsLoose != 0u)
    {
        return Shade<LoosePbrMaterial>(input);
    }
    return Shade<PbrMaterial>(input);
}
