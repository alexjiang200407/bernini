import SamplerHandle;

public struct TextureHandle
{
    public uint index;
    float4 Sample(SamplerHandle s, float2 uv) { return Sampler2D.Handle(uint2(index, s.index)).Sample(uv); }
};
