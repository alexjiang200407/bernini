import idl.TextureHandle;
import util.Tonemap;

struct SkyboxData
{
    float4x4 clipToWorld;     // inverse(viewProj) with camera translation removed
    float4x4 prevWorldToClip; // last frame's translation-free viewProj, with the skybox spin undone
    TextureCubeHandle cubeTex;
    SamplerState.Handle sampler;
    float exposure;
    float mipLevel;
};

ConstantBuffer<SkyboxData> gSkyboxData;

struct VSOut
{
    float4 position : SV_Position;
    float3 dir : TEXCOORD0; // world-space view direction (interpolated)
    float2 ndc : TEXCOORD1; // where this pixel is now; the triangle is at w = 1, so it interpolates exactly
};

struct SkyboxPSOut
{
    float4 color : SV_Target0;
    float2 motion : SV_Target1;
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
        vout.ndc = ndc;

        float4 world = mul(gSkyboxData.clipToWorld, float4(ndc, 1.0, 1.0));
        vout.dir = world.xyz / world.w;

        verts[gtid] = vout;
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }
}

[shader("pixel")]
SkyboxPSOut PSMain(VSOut input)
{
    float3 dir = normalize(input.dir);

    float3 color = gSkyboxData.cubeTex.SampleCubeLevel(gSkyboxData.sampler, dir, gSkyboxData.mipLevel).rgb;

    color = AgX(color * gSkyboxData.exposure);

    // w = 0: the sky is infinitely far, so a camera translation displaces it nowhere and only the
    // rotation between the two frames survives.
    float4 prevClip = mul(gSkyboxData.prevWorldToClip, float4(input.dir, 0.0));
    float2 prevNdc = prevClip.xy / prevClip.w;

    SkyboxPSOut output;
    output.color = float4(color, 1.0);
    output.motion = (input.ndc - prevNdc) * float2(0.5, -0.5);
    return output;
}
