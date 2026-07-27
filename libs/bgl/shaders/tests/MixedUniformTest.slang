// A uniform struct mixing plain data with resource handles -- the shape ForwardData has. The plain
// members must reach the shader through a uniform buffer while each handle stays its own binding.

import types.ComputeBuffer;

struct MixedData
{
    float4x4 viewProj;
    uint psoIndex;
    uint baseSource;
    ComputeBuffer<uint> outBuffer;
};

ConstantBuffer<MixedData> gMixed;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    gMixed.outBuffer[0] = gMixed.psoIndex;
    gMixed.outBuffer[1] = gMixed.baseSource;
    gMixed.outBuffer[2] = (uint)gMixed.viewProj[3][0];
    gMixed.outBuffer[3] = (uint)gMixed.viewProj[0][3];
}
