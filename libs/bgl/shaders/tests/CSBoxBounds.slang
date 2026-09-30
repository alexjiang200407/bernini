import lib.math.box_bounds;
import lib.types.ComputeBuffer;

// One thread per box: its eight corners in, the rect ProjectBoxBounds gives them out, as
// (lo.x, lo.y, hi.x, hi.y), with an empty rect reported as all zeros.
struct Uniforms
{
    ComputeBuffer<float4> corners;
    ComputeBuffer<float4> rects;
    float4x4 viewProj;
    uint boxCount;
};

ConstantBuffer<Uniforms> gUniforms;

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    let box = id.x;
    if (box >= gUniforms.boxCount)
    {
        return;
    }

    float3 corners[cBoxCorners];
    [unroll]
    for (uint i = 0; i < cBoxCorners; ++i)
    {
        corners[i] = gUniforms.corners[box * cBoxCorners + i].xyz;
    }

    let rect = ProjectBoxBounds(gUniforms.viewProj, corners);
    gUniforms.rects[box] = rect.IsEmpty() ? float4(0.0, 0.0, 0.0, 0.0) : float4(rect.lo, rect.hi);
}
