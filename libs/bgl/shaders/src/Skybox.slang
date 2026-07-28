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

// The covering triangle's corner, shared by both entry points so they cannot drift. Depth is 1.0 --
// the sky sits at the far plane -- and w = 1 makes ndc interpolate exactly.
VSOut EvaluateCorner(uint corner)
{
    float2 uv = float2((corner << 1) & 2, corner & 2);
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);

    VSOut vout;
    vout.position = float4(ndc, 1.0, 1.0);
    vout.ndc = ndc;

    float4 world = mul(gSkyboxData.clipToWorld, float4(ndc, 1.0, 1.0));
    vout.dir = world.xyz / world.w;

    return vout;
}

[shader("vertex")]
VSOut VSMain(uint vertexId: SV_VertexID)
{
    return EvaluateCorner(vertexId);
}

// Retained until the D3D12 backend implements IGraphicsPipeline; the vertex entry above is what
// both backends will use. Absent on WGSL, which has no mesh stage and rejects the entry at load.
#if !defined(BGL_WGSL)

[shader("mesh")]
[numthreads(3, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 3> verts, OutputIndices<uint3, 1> tris)
{
    SetMeshOutputCounts(3, 1);
    if (gtid < 3)
    {
        verts[gtid] = EvaluateCorner(gtid);
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }
}

#endif

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
