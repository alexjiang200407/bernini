import idl.GrassChunk;
import idl.GrassChunkRef;
import idl.GrassClump;
import idl.GrassDraw;
import idl.GrassLook;
import lib.types.EntryBuffer;
import lib.types.UploadBuffer;

// What the grass pass binds for one batch of a view's grass list: read by the geometry stage, and by
// a grass pixel program for its blade's look.
public struct GrassData
{
    public UploadBuffer<GrassDraw> draws;
    public UploadBuffer<GrassChunkRef> chunkRefs;
    public EntryBuffer<GrassLook> looks;

    // Scene ranges, but addressed by the global indices the chunk references and chunks store.
    public StructuredBuffer<GrassChunk, ScalarDataLayout>.Handle chunks;
    public StructuredBuffer<GrassClump, ScalarDataLayout>.Handle clumps;

    public float3 cameraPos;

    // What one world unit spans on the render grid, in pixels, at a distance of one.
    public float pixelsPerUnit;

    // The view's wind (GrassWind): the direction's xz, unit.
    public float2 windDirection;
    public float windStrength;
    public float windGustScale;
    public float windGustSpeed;
    public float windGustStrength;

    // This dispatch's run of the chunk references, laid out dispatchWidth groups to a row.
    public uint firstRef;
    public uint refCount;
    public uint dispatchWidth;
};

public ConstantBuffer<GrassData> grassData;
