// One level of the HZB from the one above it, or level 0 from the depth buffer: each texel the
// farthest depth of the 2x2 source texels it covers -- the minimum, under reversed-Z. A level is
// half its source rounded up, so the last texel of an odd source axis covers one real texel and its
// clamped repeat; nothing is dropped.
struct HzbReduceData
{
    Texture2D.Handle source;
    uint2 sourceSize;
    uint2 destSize;
};

ConstantBuffer<HzbReduceData> gHzbReduceData;

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

[shader("pixel")]
float PSMain(VSOut input) : SV_Target
{
    let dst = uint2(input.position.xy);
    let last = gHzbReduceData.sourceSize - uint2(1u, 1u);
    let origin = min(dst * 2u, last);

    float farthest = 1.0;
    [unroll]
    for (uint y = 0; y < 2u; ++y)
    {
        [unroll]
        for (uint x = 0; x < 2u; ++x)
        {
            let texel = min(origin + uint2(x, y), last);
            farthest = min(farthest, gHzbReduceData.source.Load(int3(texel, 0)).r);
        }
    }
    return farthest;
}
