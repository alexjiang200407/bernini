import bgl.MaterialReader;
import bgl.SurfaceLight;
import bgl.WaterSurfaceSource;
import idl.GameSurfaceRecord;
import idl.IMaterial;
import idl.RawEntry;
import idl.Terrain;
import lib.forward.GameSurface;
import lib.forward.MaterialData;
import lib.forward.WaterData;
import lib.forward.common;
import lib.terrain.heightfield;

// What GroundDepth and ViewDepth answer where there is nothing to measure to: no terrain under the
// pixel, or only the sky behind it. Far past any shore a surface draws.
static const float cNoWaterBottom = 1.0e6;

// The world position the scene's depth holds at `position`, a target-absolute pixel.
float3 SceneWorldAt(float2 position, float depth)
{
    let rect = waterData.viewportRect;
    let ndc = float2((position.x - rect.x) * rect.z * 2.0 - 1.0, 1.0 - (position.y - rect.y) * rect.w * 2.0);
    let world = mul(waterData.invViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

Texture2D.Handle WaterTerrainHeights(uint slot)
{
    switch (slot)
    {
    case 0u:
        return waterData.terrainHeights0;
    case 1u:
        return waterData.terrainHeights1;
    case 2u:
        return waterData.terrainHeights2;
    default:
        return waterData.terrainHeights3;
    }
}

bool TerrainHolds(Terrain terrain, float2 xz)
{
    let origin = terrain.originAndCellSize.xz;
    let extent = float2(terrain.samplesX - 1u, terrain.samplesZ - 1u) * terrain.originAndCellSize.w;
    return all(xz >= origin) && all(xz <= origin + extent);
}

// The engine's side of the water contract: the arena reader's, and the frame water is drawn over.
public struct WaterArenaReader : IWaterMaterialReader
{
    MaterialArenaReader inner;

    public float4 Sample<S : ISlot>(S slot, float2 uv) { return inner.Sample(slot, uv); }
    public float2 Uv() { return inner.Uv(); }
    public float2 Uv1() { return inner.Uv1(); }
    public bool HasUv1() { return inner.HasUv1(); }
    public float3 WorldPos() { return inner.WorldPos(); }
    public float3 WorldNormal() { return inner.WorldNormal(); }
    public float4 WorldTangent() { return inner.WorldTangent(); }
    public float3 CameraPos() { return inner.CameraPos(); }

    public float ViewDepth()
    {
        let position = inner.input.position.xy;
        let depth = waterData.sceneDepth.Load(int3(int2(position), 0)).r;
        if (depth >= 1.0)
        {
            return cNoWaterBottom;
        }
        return distance(SceneWorldAt(position, depth), inner.WorldPos());
    }

    public float GroundDepth()
    {
        let surface = inner.WorldPos();
        for (uint slot = 0; slot < min(waterData.terrainCount, cMaxWaterTerrains); ++slot)
        {
            let terrain = waterData.terrains[waterData.terrainRecords[slot]];
            if (TerrainHolds(terrain, surface.xz))
            {
                return surface.y - TerrainHeightAt(terrain, WaterTerrainHeights(slot), waterData.heightSampler, surface.xz);
            }
        }
        return cNoWaterBottom;
    }

    public float Time() { return waterData.time; }
}

// A water surface's record, loaded as LitGameRecord loads a lit one's.
public struct WaterGameRecord<G : IWaterSurfaceSource>
{
    GameSurfaceRecord engine;
    G.MaterialParams params;
    uint byteOffset;

    public static WaterGameRecord<G> Load(uint byteOffset)
    {
        WaterGameRecord<G> record;
        record.engine = materialData.materials.LoadRecordAs<GameSurfaceRecord>(RawEntry<IMaterial>.At(byteOffset));
        record.params = materialData.materials.LoadAt<G.MaterialParams>(byteOffset + cGameSurfaceParamsByteOffset);
        record.byteOffset = byteOffset;
        return record;
    }

    public float4 Shade(ForwardVSOut input, bool isFrontFace)
    {
        WaterArenaReader reader;
        reader.inner = ArenaReaderAt(byteOffset, input, isFrontFace);
        return G.Shade(reader, MaterialSurfaceLight(), params);
    }
}

public struct NullWaterSurfaceParams
{
    public uint unused;
};

// The water contract's null surface, which the build validates the water program's shape against
// (programs/forward/GameSurfaceShapes.slang). It reads every frame input, so each is compiled.
// Nothing draws it.
public struct NullWaterSurface : IWaterSurfaceSource
{
    public typealias MaterialParams = NullWaterSurfaceParams;

    public static float4 Shade<R : IWaterMaterialReader, L : ISurfaceLight>(R reader, L light, NullWaterSurfaceParams params)
    {
        return float4(reader.ViewDepth(), reader.GroundDepth(), reader.Time(), 1.0);
    }
}

public extension MaterialData
{
    // The colour program a water surface's buckets draw through. No depth is attached, so the
    // occlusion test is here: a scene nearer than the water hides it. The rgb is weighted by
    // Shade's a for the premultiplied blend; the velocity is the water surface's own.
    public ForwardPSOut GameWaterProgram<G : IWaterSurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let sceneDepth = waterData.sceneDepth.Load(int3(int2(input.position.xy), 0)).r;
        if (sceneDepth < input.position.z)
        {
            discard;
        }

        let shaded = WaterGameRecord<G>.Load(input.materialOffset).Shade(input, isFrontFace);
        let coverage = saturate(shaded.a);

        ForwardPSOut output;
        output.color = float4(shaded.rgb * exposure * coverage, coverage);
        output.motion = ComputeMotionVector(input);
        return output;
    }
}
