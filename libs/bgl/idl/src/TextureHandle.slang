import SamplerHandle;

public struct TextureHandle
{
    public uint index;

    public float4 Sample(SamplerHandle s, float2 uv) { return Sampler2D.Handle(uint2(index, s.index)).Sample(uv); }

    public float4 Sample3D(SamplerHandle s, float3 uvw) { return Sampler3D.Handle(uint2(index, s.index)).Sample(uvw); }

    public float4 SampleLevel(SamplerHandle s, float2 uv, float level)
    {
        return Sampler2D.Handle(uint2(index, s.index)).SampleLevel(uv, level);
    }

    public float4 SampleCubeLevel(SamplerHandle s, float3 uvw, float level)
    {
        return SamplerCube.Handle(uint2(index, s.index)).SampleLevel(uvw, level);
    }

    public float4 SampleCube(SamplerHandle s, float3 uvw) { return SamplerCube.Handle(uint2(index, s.index)).Sample(uvw); }
};
