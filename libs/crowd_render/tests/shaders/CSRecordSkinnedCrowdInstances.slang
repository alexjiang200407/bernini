import crowd_render.CrowdInstanceWriter;
import crowd_render_tests.RecordingCrowdBlock;

struct Uniforms
{
    RecordingBlock block;
    CrowdWriterParams params;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint slot: SV_DispatchThreadID)
{
    if (slot >= gUniforms.block.Capacity())
        return;
    SkinnedCrowdInstanceWriter.Write(gUniforms.block, gUniforms.params, slot);
}
