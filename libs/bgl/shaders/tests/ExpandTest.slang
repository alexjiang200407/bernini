// A stand-in for the meshlet-expansion kernel: a compute shader that writes a vertex stream and the
// indirect draw arguments the raster stage then consumes. The real kernel derives these from meshlet
// data; this writes a fixed triangle so the compute -> drawIndirect loop can be proven on its own.

import types.ComputeBuffer;

struct ExpandData
{
    ComputeBuffer<float2> positions;
    ComputeBuffer<uint> drawArgs; // {vertexCount, instanceCount, firstVertex, firstInstance}
};

ConstantBuffer<ExpandData> gExpand;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    gExpand.positions[0] = float2(0.0, 0.8);
    gExpand.positions[1] = float2(-0.8, -0.8);
    gExpand.positions[2] = float2(0.8, -0.8);

    gExpand.drawArgs[0] = 3u;
    gExpand.drawArgs[1] = 1u;
    gExpand.drawArgs[2] = 0u;
    gExpand.drawArgs[3] = 0u;
}
