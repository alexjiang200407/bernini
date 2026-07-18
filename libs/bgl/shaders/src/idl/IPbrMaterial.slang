// THIS IS A FILE GENERATED FROM IPbrMaterial.slang. DO NOT EDIT MANUALLY

public interface IPbrMaterial
{
    static This Load(uint index);

    float4 GetBaseColor(SamplerState.Handle s, float2 uv);
    float3 GetORM(SamplerState.Handle s, float2 uv);
    float2 GetNormalXY(SamplerState.Handle s, float2 uv);
};
