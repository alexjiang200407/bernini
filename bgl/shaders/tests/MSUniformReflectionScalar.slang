public struct AllScalarUniforms
{
    public float f1;
    public float2 f2;
    public float3 f3;
    public float4 f4;
    public int i1;
    public int2 i2;
    public int3 i3;
    public int4 i4;
    public uint u1;
    public uint2 u2;
    public uint3 u3;
    public uint4 u4;
    public float4x4 mat;
};

ConstantBuffer<AllScalarUniforms> gUniforms;

[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void main()
{}
