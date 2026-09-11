import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceSource;
import idl.GameSurfaceRecord;
import idl.IMaterial;
import idl.RawEntry;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;
import lib.forward.common;
import lib.math.HashedAlpha;
import lib.types.Texture;

// The engine's side of the contract: every handle a surface reads is dereferenced here, and the
// surface sees a slot index and a UV, never the arena.
public struct MaterialArenaReader : IMaterialReader
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

// The same reader with every sample displaced along the mip chain, which is how the engine reads a
// surface's coverage at a level the hardware did not pick. Coverage is arithmetic over its samples,
// so evaluating it through this is what gives the hash a finer read and the steepening a local mean
// without ISurfaceSource carrying a word about either.
//
// A wrapper rather than a bias on MaterialArenaReader: that would put SampleBias on the material
// sampling path of every row to serve one. The interpolants forward, so there is one definition of
// each and the one method that differs is the only one written out.
public struct BiasedArenaReader : IMaterialReader
{
    MaterialArenaReader inner;
    float lodBias;

    public float4 Sample<S : ISlot>(S slot, float2 uv)
    {
        return materialData.GetTextureAt(inner.byteOffset, slot.Index()).SampleBias(materialData.anisoLinearWrapSampler, uv, lodBias);
    }

    public float2 Uv() { return inner.Uv(); }
    public float3 WorldPos() { return inner.WorldPos(); }
    public float3 WorldNormal() { return inner.WorldNormal(); }
    public float4 WorldTangent() { return inner.WorldTangent(); }
    public float3 CameraPos() { return inner.CameraPos(); }
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

    MaterialArenaReader Reader(ForwardVSOut input)
    {
        MaterialArenaReader reader;
        reader.byteOffset = byteOffset;
        reader.input = input;
        return reader;
    }

    BiasedArenaReader BiasedReader(ForwardVSOut input, float lodBias)
    {
        BiasedArenaReader reader;
        reader.inner = Reader(input);
        reader.lodBias = lodBias;
        return reader;
    }

    public float Coverage(ForwardVSOut input) { return G.Coverage(Reader(input), params); }
    public PbrSurface Evaluate(ForwardVSOut input) { return G.Evaluate(Reader(input), params); }
    public float AlphaCutoff() { return engine.alphaCutoff; }

    public float CoverageAtBias(ForwardVSOut input, float lodBias) { return G.Coverage(BiasedReader(input, lodBias), params); }

    // The mip-0 texel counts of the texture the surface declared its coverage is measured against.
    // @pre engine.coverageSlot != cNoCoverageSlot, which CreateSurfaceMaterial refuses the hashed
    // layer without.
    public float2 CoverageDimensions() { return materialData.GetTextureAt(byteOffset, engine.coverageSlot).GetDimensions(); }
}

public struct NullSurfaceParams
{
    public uint unused;
};

// What an unfilled slot's pipelines compile against: a flat mid-grey dielectric, fully covering. The
// tree ships it under every game.slotN, and a registered surface's binding module shadows that file.
//
// Not something anything draws. A material only carries slot N's MaterialType if surface N
// registered -- CreateSurfaceMaterial refuses a name it has no SurfaceType for -- so an unfilled
// slot's rows have no instances. This exists so the reserved rows link whether the slot is filled
// or not, and its colour is never on screen.
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

    // Stochastic coverage in place of a cutoff, as ShadeHashedAlpha does it for a PBR record: the
    // fragment survives with probability equal to its coverage and writes real depth, so this is the
    // blend only as the ensemble over pixels and frames, and only with temporal AA running.
    //
    // The surface's cutoff is deliberately unused, for the reason the PBR path leaves it alone: a
    // cutoff is the thing being replaced.
    public float4 ShadeGameHashedAlpha<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = GameRecord<G>.Load(input.materialOffset);

        // Coverage is evaluated twice against two levels rather than sampled twice, because what a
        // surface returns is arithmetic and not a texel: the hash wants the finer read where a
        // sub-texel strand still has shape, and the steepening wants the local mean an octave
        // coarser than that. Why those two levels, and why the slope is measured in the sampled
        // level's own texels, is docs/taa.md.
        let texDim = record.CoverageDimensions() * exp2(c_HashedAlphaLodBias);
        let hashAlpha = record.CoverageAtBias(input, c_HashedAlphaLodBias);
        let meanAlpha = record.CoverageAtBias(input, c_HashedAlphaLodBias + c_MinifiedAlphaMeanBias);

        float alpha = SharpenMinifiedAlpha(hashAlpha, meanAlpha, input.uv, texDim);

        if (alpha < HashedAlphaThreshold(input.worldPos, alphaHashSeed))
        {
            discard;
        }

        return float4(ShadeSurface(input, record.Evaluate(input), isFrontFace).rgb, 0.0);
    }

    // Premultiplied, against the transparent PSOs' SrcBlend = One; see ShadeSurfaceBlended.
    public float4 ShadeGameBlended<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = GameRecord<G>.Load(input.materialOffset);

        return ShadeSurfaceBlended(input, record.Evaluate(input), isFrontFace);
    }
}
