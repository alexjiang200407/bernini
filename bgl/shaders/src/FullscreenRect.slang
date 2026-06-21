import uniforms.SceneData;
import db.StaticMesh;

struct VSOut
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

ConstantBuffer<SceneData<StaticMeshInstance>> sceneData;

[shader("mesh")]
[numthreads(3, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint gtid : SV_GroupThreadID,
    OutputVertices<VSOut, 3> verts,
    OutputIndices<uint3, 1> tris
)
{
    SetMeshOutputCounts(3, 1);
    if (gtid < 3)
    {
        VSOut vout;
        vout.uv = float2((gtid << 1) & 2, gtid & 2);
        vout.position = float4(vout.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

        verts[gtid] = vout;
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
    }
}


[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target {
    return float4(1.0, 1.0, 1.0, 1.0);
}