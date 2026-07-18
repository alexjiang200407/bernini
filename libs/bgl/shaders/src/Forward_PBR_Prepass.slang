import forward.common;
import forward.MaterialData;
import idl.PbrMaterial;

// Depth-only pre-pass for a self-occluding baked-triplet blend material. No colour output.
[shader("pixel")]
void PSMain(ForwardVSOut input)
{
    PrepassDiscard<PbrMaterial>(input);
}
