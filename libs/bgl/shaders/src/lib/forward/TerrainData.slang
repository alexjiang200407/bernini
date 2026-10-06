import idl.Terrain;
import idl.TerrainNodeBounds;
import lib.types.EntryBuffer;

// What the terrain pass binds for one terrain: read by its geometry stage alone. The pixel side is
// the material's own opaque program, which reads a ForwardVSOut and nothing of this.
public struct TerrainData
{
    public EntryBuffer<Terrain> terrains;

    // Every terrain's node bounds, the lowest and highest world y of each node level-major, but
    // addressed from firstNodeBound: a scene range, read by the global index.
    public StructuredBuffer<TerrainNodeBounds, ScalarDataLayout>.Handle nodeBounds;

    // This terrain's samples, and the clamping sampler they are read through.
    public Texture2D.Handle heights;
    public SamplerState.Handle heightSampler;

    // The draw's frustum, normalized planes with the positive half-space inside: the terrain's own
    // copy, so a view with nothing in the instance cull still culls its ground.
    public float4 frustumPlanes[6];

    public float3 cameraPos;

    // What one world unit spans on the render grid, in pixels, at a distance of one, and the
    // view's scale on every authored threshold (LodSelectionDesc::pixelScale).
    public float pixelsPerUnit;
    public float lodPixelScale;

    // Which terrain this dispatch draws, where its node bounds start, how many nodes it has, and
    // how many amplification groups a row of the dispatch holds.
    public uint terrain;
    public uint firstNodeBound;
    public uint nodeCount;
    public uint dispatchWidth;
};

public ConstantBuffer<TerrainData> terrainData;
