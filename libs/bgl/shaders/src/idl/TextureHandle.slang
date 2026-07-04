// THIS IS A FILE GENERATED FROM TextureHandle.slang. DO NOT EDIT MANUALLY

public struct TextureHandle
{
    uint index;

    float4 Sample(SamplerState s, float2 uv) { return Texture2D.Handle(index).Sample(s, uv); }
};
