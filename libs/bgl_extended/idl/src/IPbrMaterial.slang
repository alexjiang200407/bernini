public interface IPbrMaterial
{
    static This Load(uint byteOffset);

    // No sampler parameter: an implementation reaches its own. A material interface that named a
    // binding type could not be shared with a renderer that has no such handle.
    float4 GetBaseColor(float2 uv);
    float3 GetORM(float2 uv);
    float2 GetNormalXY(float2 uv);
    float GetTransmission();

    float4 GetSpecular();
};
