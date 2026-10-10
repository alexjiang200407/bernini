// Water Scene Copy's pixel stage, over programs.screen.FullscreenRect's triangle: the scene colour
// the opaque phases drew, texel for texel into the target's copy. Forward Water draws into scene
// colour, so it refracts through this.

struct WaterSceneCopyData
{
    Texture2D.Handle sceneColor;
};

ConstantBuffer<WaterSceneCopyData> gWaterSceneCopyData;

// FullscreenRect's mesh output; uv is unused, since SV_Position is target-absolute and the copy has
// scene colour's extent.
struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
};

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    return gWaterSceneCopyData.sceneColor.Load(int3(int2(input.position.xy), 0));
}
