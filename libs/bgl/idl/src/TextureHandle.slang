public struct TextureHandle
{
    uint index;

    float4 Sample(SamplerState s, float2 uv) { return Texture2D.Handle(index).Sample(s, uv); }
};
