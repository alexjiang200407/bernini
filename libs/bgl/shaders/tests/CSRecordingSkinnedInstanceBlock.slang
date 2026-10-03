import RecordingInstanceBlock;
import SkinnedMeshInstanceWriterProbe;

struct Uniforms
{
    RecordingBlock block;
    SkinnedMeshInstanceWriterProbe.Params params;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(8, 1, 1)]
void main(uint slot: SV_DispatchThreadID)
{
    if (slot >= gUniforms.block.Capacity())
        return;
    SkinnedMeshInstanceWriterProbe.Write(gUniforms.block, gUniforms.params, slot);
}
