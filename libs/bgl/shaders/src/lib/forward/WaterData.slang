import idl.Terrain;
import lib.types.EntryBuffer;

// A cbuffer of its own, as toonData is, so no program but water's carries it.
public struct WaterData
{
    // The scene's depth as the phases before water left it: terrain, world, grass and characters.
    public Texture2D.Handle sceneDepth;

    // The scene colour the same phases drew, copied by Water Scene Copy before this phase.
    public Texture2D.Handle sceneColor;

    // Inverse of the view's viewProj, jitter included, so a reconstructed pixel lands where the
    // phases before rasterized it.
    public float4x4 invViewProj;

    // This draw's viewport as (minX, minY, 1/width, 1/height): SV_Position arrives in
    // target-absolute pixels and the reconstruction needs viewport-relative NDC.
    public float4 viewportRect;

    public float time;

    // How many of the view's terrains the four slots below hold, and each one's record in
    // `terrains` with its samples. GroundDepth reads the first whose footprint holds the pixel.
    public uint terrainCount;
    public uint4 terrainRecords;
    public EntryBuffer<Terrain> terrains;
    public Texture2D.Handle terrainHeights0;
    public Texture2D.Handle terrainHeights1;
    public Texture2D.Handle terrainHeights2;
    public Texture2D.Handle terrainHeights3;
    public SamplerState.Handle heightSampler;
};

public ConstantBuffer<WaterData> waterData;

// The most terrains GroundDepth reads; a fifth of a view's is not read.
public static const uint cMaxWaterTerrains = 4;
