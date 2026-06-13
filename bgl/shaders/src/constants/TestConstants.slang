import types.Vertex;

public struct PushConstants
{
    public StructuredBuffer<Vertex>.Handle vertexBuffer;
    public float3 color;
};

[[vk::push_constant]]
public PushConstants gPushConstants;
