import bgl.SurfaceLight;
import idl.ToonShadingRigBlock;

// What only the toon character's programs read: its own sun, apart from the one every other model
// reads (MaterialData), and the view's evaluated toon shading rig blocks. A cbuffer of its own so the
// programs that never import it -- every engine material's -- carry none of it.
public struct ToonData
{
    // The direction the toon sun travels, normalized, and its colour scaled by its intensity.
    public float3 sunDirection;
    public float3 sunRadiance;

    // Named by a toon character draw's vertices (ToonVSOut::toonShadingRigSlot).
    public RWStructuredBuffer<ToonShadingRigBlock>.Handle toonShadingRigBlocks;
};

public ConstantBuffer<ToonData> toonData;

// The toon character model's light: the toon sun and no environment, which the model never reads.
public struct ToonSurfaceLight : ISurfaceLight
{
    public float3 SunDirection() { return toonData.sunDirection; }
    public float3 SunRadiance() { return toonData.sunRadiance; }
    public float3 Irradiance(float3 dir) { return float3(0.0); }
    public float3 Reflection(float3 dir, float roughness) { return float3(0.0); }
}
