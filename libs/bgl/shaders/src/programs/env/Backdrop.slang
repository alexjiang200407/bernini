struct BackdropData
{
    // Scene-linear, drawn as they are: presentation, never exposed and never lighting.
    float3 bottom;
    float3 top;
};

ConstantBuffer<BackdropData> gBackdropData;

struct VSOut
{
    float4 position : SV_Position;
    float height : HEIGHT; // 0 at the frame's bottom edge, 1 at its top
};

struct BackdropPSOut
{
    float4 color : SV_Target0;
    float4 motion : SV_Target1;
};

[shader("mesh")]
[numthreads(3, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 3> verts, OutputIndices<uint3, 1> tris)
{
    SetMeshOutputCounts(3, 1);
    if (gtid < 3)
    {
        float2 uv = float2((gtid << 1) & 2, gtid & 2);
        float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);

        VSOut vout;
        vout.position = float4(ndc, 1.0, 1.0);
        vout.height = ndc.y * 0.5 + 0.5;

        verts[gtid] = vout;
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }
}

[shader("pixel")]
BackdropPSOut PSMain(VSOut input)
{
    BackdropPSOut output;
    output.color = float4(lerp(gBackdropData.bottom, gBackdropData.top, saturate(input.height)), 1.0);
    // Fixed to the screen, so nothing moves it.
    output.motion = float4(0.0, 0.0, 0.0, 0.0);
    return output;
}
