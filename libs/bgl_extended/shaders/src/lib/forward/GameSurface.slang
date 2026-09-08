import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;
import idl.GameSurfaceRecord;
import idl.IMaterial;
import idl.RawEntry;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;
import lib.forward.common;

// The engine's side of the contract: every handle a surface reads is dereferenced here, and the
// surface sees a slot index and a UV, never the arena.
public struct ArenaReader : IMaterialReader
{
    uint byteOffset;
    ForwardVSOut input;

    public float4 Sample<S : ISlot>(S slot, float2 uv)
    {
        return materialData.GetTextureAt(byteOffset, slot.Index()).Sample(materialData.anisoLinearWrapSampler, uv);
    }

    public float2 Uv() { return input.uv; }
    public float3 WorldPos() { return input.worldPos; }
    public float3 WorldNormal() { return input.normal; }
    public float4 WorldTangent() { return input.tangent; }
    public float3 CameraPos() { return materialData.cameraPos; }
}

// A game surface's record: the engine's fixed part, then the surface's own parameters, loaded as the
// struct the surface declared them as.
public struct GameRecord<G : ISurfaceSource>
{
    GameSurfaceRecord engine;
    G.MaterialParams params;
    uint byteOffset;

    public static GameRecord<G> Load(uint byteOffset)
    {
        GameRecord<G> record;
        record.engine = materialData.materials.LoadRecordAs<GameSurfaceRecord>(RawEntry<IMaterial>.At(byteOffset));
        record.params = materialData.materials.LoadAt<G.MaterialParams>(byteOffset + cGameSurfaceParamsByteOffset);
        record.byteOffset = byteOffset;
        return record;
    }

    ArenaReader Reader(ForwardVSOut input)
    {
        ArenaReader reader;
        reader.byteOffset = byteOffset;
        reader.input = input;
        return reader;
    }

    public float Coverage(ForwardVSOut input) { return G.Coverage(Reader(input), params); }
    public PbrSurface Evaluate(ForwardVSOut input) { return G.Evaluate(Reader(input), params); }
    public float AlphaCutoff() { return engine.alphaCutoff; }
}

public struct NullSurfaceParams
{
    public uint unused;
};

// What an unfilled slot draws: a flat mid-grey dielectric, fully covering. The tree ships it under
// every game.slotN, and a registered surface's binding module shadows that file.
public struct NullSurface : ISurfaceSource
{
    public typealias MaterialParams = NullSurfaceParams;

    public static float Coverage<R : IMaterialReader>(R reader, NullSurfaceParams params) { return 1.0; }

    public static PbrSurface Evaluate<R : IMaterialReader>(R reader, NullSurfaceParams params)
    {
        PbrSurface surface = PbrSurface();
        surface.baseColor = float4(0.5, 0.5, 0.5, 1.0);
        return surface;
    }
}

public extension MaterialData
{
    public float4 ShadeGame<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = GameRecord<G>.Load(input.materialOffset);

        return ShadeSurface(input, record.Evaluate(input), isFrontFace);
    }

    public float4 ShadeGameAlphaTested<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = GameRecord<G>.Load(input.materialOffset);

        if (record.Coverage(input) < record.AlphaCutoff())
        {
            discard;
        }

        return ShadeSurface(input, record.Evaluate(input), isFrontFace);
    }

    // Premultiplied, against the transparent PSOs' SrcBlend = One; see ShadeSurfaceBlended.
    public float4 ShadeGameBlended<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = GameRecord<G>.Load(input.materialOffset);

        return ShadeSurfaceBlended(input, record.Evaluate(input), isFrontFace);
    }
}
