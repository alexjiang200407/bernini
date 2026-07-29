// THIS IS A FILE GENERATED FROM IPbrMaterial.slang. DO NOT EDIT MANUALLY

import SamplerHandle;

public interface IPbrMaterial
{
    static This Load(uint index);

    float4 GetBaseColor(SamplerHandle s, float2 uv);
    float3 GetORM(SamplerHandle s, float2 uv);
    float2 GetNormalXY(SamplerHandle s, float2 uv);
};
