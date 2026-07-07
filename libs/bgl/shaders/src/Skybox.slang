import idl.TextureHandle;
import idl.SamplerHandle;
import util.Tonemap;

struct SkyboxData
{
    float4x4 clipToWorld; // inverse(viewProj) with camera translation removed
    TextureHandle cubeTex;
    SamplerHandle sampler;
    float exposure;
    float mipLevel;
};

ConstantBuffer<SkyboxData> skyboxData : register(b0);

struct VSOut
{
    float4 position : SV_Position;
    float3 dir : TEXCOORD0; // world-space view direction (interpolated)
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

        float4 world = mul(skyboxData.clipToWorld, float4(ndc, 1.0, 1.0));
        vout.dir = world.xyz / world.w;

        verts[gtid] = vout;
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    float3 dir = normalize(input.dir);

    float3 color = skyboxData.cubeTex.SampleCubeLevel(skyboxData.sampler, dir, skyboxData.mipLevel).rgb;

    color = AgX(color * skyboxData.exposure);
    return float4(color, 1.0);
}
