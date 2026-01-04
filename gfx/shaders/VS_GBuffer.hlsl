#include "common/Mesh.hlsli"

StructuredBuffer<MeshInstance> instanceBuffer : register(t0, space0);
StructuredBuffer<Vertex> vertexBuffer : register(t3, space0);

cbuffer FrameConstants : register(b0, space0)
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    uint instanceCount;
};

struct VSOutput
{
    float4 position : SV_POSITION;
};

// Remove VSInput. Use SV_VertexID to fetch from vertexBuffer manually.
VSOutput VS_GBuffer(uint vID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output;

    MeshInstance instance = instanceBuffer[instanceID];

    float3 rawPosition = vertexBuffer[vID].position;

    float4 worldPos = mul(instance.modelTransform, float4(rawPosition, 1.0f));
    float4 viewPos = mul(viewMatrix, worldPos);
    output.position = mul(projMatrix, viewPos);

    return output;
}
