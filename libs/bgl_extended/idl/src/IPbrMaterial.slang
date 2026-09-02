public interface IPbrMaterial
{
    static This Load(uint byteOffset);

    float4 GetBaseColor(float2 uv);
    float3 GetORM(float2 uv);
    float2 GetNormalXY(float2 uv);
    float GetTransmission();

    float4 GetSpecular();
};
