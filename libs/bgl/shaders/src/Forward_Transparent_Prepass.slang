import forward.common;
import forward.MaterialData;
import idl.PbrMaterial;
import idl.LoosePbrMaterial;

// Depth-only pre-pass for a self-occluding blend material, shared by both material types. No colour
// output; the pipeline binds no render target.
[shader("pixel")]
void PSMain(ForwardVSOut input)
{
    if (input.materialIsLoose != 0u)
    {
        PrepassDiscard<LoosePbrMaterial>(input);
    }
    else
    {
        PrepassDiscard<PbrMaterial>(input);
    }
}
