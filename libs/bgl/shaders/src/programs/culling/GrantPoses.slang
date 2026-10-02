import idl.Constants;
import idl.InstanceLod;
import idl.PosePool;
import lib.types.ComputeBuffer;

// After ChoosePoses: grants the right to draw per instance next frame to the first placements that
// asked for it, as many as the budget leaves once this frame's are counted. The grant is a bit of
// the placement's word, which next frame's ChoosePoses reads as last frame's. One thread a request;
// the dispatch covers every automatic placement, since any of them may have asked.

struct Uniforms
{
    ComputeBuffer<PosePool> pool;
    ComputeBuffer<uint> requests;
    ComputeBuffer<InstanceLod> lodCurrent;

    uint budget;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(cHistogramGroupSize, 1, 1)]
void main(uint threadIdx: SV_DispatchThreadID)
{
    let requested = gUniforms.pool[0].requested.load();
    let posed = min(gUniforms.pool[0].posed.load(), gUniforms.budget);
    let slack = gUniforms.budget - posed;

    if (threadIdx >= requested || threadIdx >= slack)
    {
        return;
    }

    let placement = gUniforms.requests[threadIdx];
    gUniforms.lodCurrent[placement] = gUniforms.lodCurrent[placement].WithGrant();
}
