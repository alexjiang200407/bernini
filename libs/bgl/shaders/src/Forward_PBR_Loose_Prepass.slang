import forward.common;
import forward.MaterialData;
import idl.LoosePbrMaterial;

// Depth-only pre-pass for a self-occluding loose blend material. No colour output.
[shader("pixel")]
void PSMain(ForwardVSOut input)
{
    PrepassDiscard<LoosePbrMaterial>(input);
}
