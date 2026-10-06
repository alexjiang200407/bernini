import bgl.LitSurfaceSource;
import bgl.MaterialReader;
import bgl.SurfaceLight;
import bgl.ToonEnvironmentSurface;
import idl.ToonShadingRigBlock;
import lib.math.ToonShading;

// What only the toon models' programs read: their own sun, apart from the one every other model
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

// The toon models' light: the toon sun and no environment, which neither model reads.
public struct ToonSurfaceLight : ISurfaceLight
{
    public float3 SunDirection() { return toonData.sunDirection; }
    public float3 SunRadiance() { return toonData.sunRadiance; }
    public float3 Irradiance(float3 dir) { return float3(0.0); }
    public float3 Reflection(float3 dir, float roughness) { return float3(0.0); }
}

/**
 * An environment surface as a lit one, its lighting the engine's toon environment model under the
 * toon sun. What a registered environment surface is bound as, so it draws through the lit
 * programs -- the record, the reader, every layer and grass -- and the model adds no program
 * family of its own. Those programs hand it the material light, which it sets aside: a toon
 * model is lit by the toon sun alone, and here, unlike the character's own programs, nothing else
 * can hand it one.
 */
public struct ToonEnvironmentLit<G : IToonEnvironmentSurfaceSource> : ILitSurfaceSource
{
    public typealias MaterialParams = G.MaterialParams;

    public static float Coverage<R : IMaterialReader>(R reader, MaterialParams params) { return G.Coverage(reader, params); }

    public static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, MaterialParams params)
    {
        return ShadeToonEnvironment(G.Evaluate(reader, params), ToonSurfaceLight(), normalize(reader.WorldNormal()));
    }
};
