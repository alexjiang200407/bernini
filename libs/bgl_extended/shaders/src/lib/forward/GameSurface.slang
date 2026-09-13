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

// A data slot gathered from its record's channel routes: component c of slot `slot` reads the
// routeChannels-named component of routeTextures[slot * cGameSurfaceRouteChannels + c], as
// SampleLooseChannel routes the engine's own channels. This exists so a slot packed from other
// textures' channels -- an ORM split across two maps -- draws unbaked with no CPU composite:
// rewiring a channel rewrites the record instead of rebuilding a texture. The route handles sit
// contiguously behind the slot handles in the arena's typed view, which is what GetTextureAt's
// index arithmetic rests on.
// Whether the record composes `slot` from its routes, written at record build (GameSurfaceRecord
// .routedMask): the whole-bound and unbound cases stay on the single fetch they always took.
bool SlotIsRouted(uint byteOffset, uint slot)
{
    let mask = materialData.materials.LoadAt<uint>(byteOffset + cGameSurfaceRoutedMaskByteOffset);
    return ((mask >> slot) & 1u) != 0u;
}

float4 SampleRoutedSlot(uint byteOffset, uint slot, float2 uv)
{
    float4 gathered;
    [ForceUnroll]
    for (uint c = 0; c < cGameSurfaceRouteChannels; ++c)
    {
        let route = slot * cGameSurfaceRouteChannels + c;
        let source = materialData.GetTextureAt(byteOffset, cGameSurfaceTextureSlots + route);
        let channel = materialData.materials.LoadAt<uint>(byteOffset + cGameSurfaceRouteChannelsByteOffset + route * 4);
        gathered[c] = SelectComponent(source.Sample(materialData.anisoLinearWrapSampler, uv), channel);
    }
    return gathered;
}

// SampleRoutedSlot displaced along the mip chain, for BiasedArenaReader -- a second function for
// the reason that reader is a wrapper: a bias parameter here would put SampleBias on every row's
// material sampling path to serve the coverage read.
float4 SampleRoutedSlotBias(uint byteOffset, uint slot, float2 uv, float lodBias)
{
    float4 gathered;
    [ForceUnroll]
    for (uint c = 0; c < cGameSurfaceRouteChannels; ++c)
    {
        let route = slot * cGameSurfaceRouteChannels + c;
        let source = materialData.GetTextureAt(byteOffset, cGameSurfaceTextureSlots + route);
        let channel = materialData.materials.LoadAt<uint>(byteOffset + cGameSurfaceRouteChannelsByteOffset + route * 4);
        gathered[c] = SelectComponent(source.SampleBias(materialData.anisoLinearWrapSampler, uv, lodBias), channel);
    }
    return gathered;
}

// The engine's side of the contract: every handle a surface reads is dereferenced here, and the
// surface sees a slot index and a UV, never the arena.
public struct MaterialArenaReader : IMaterialReader
{
    uint byteOffset;
    ForwardVSOut input;
    bool isFrontFace;

    public float4 Sample<S : ISlot>(S slot, float2 uv)
    {
        // S.Routed() is a compile-time constant per specialization, so only a data slot carries
        // the branch at all -- and its mask load is one scalar, uniform per material, keeping a
        // whole-bound slot (the shipped, baked case) at the single fetch.
        if (S.Routed() && SlotIsRouted(byteOffset, slot.Index()))
        {
            return SampleRoutedSlot(byteOffset, slot.Index(), uv);
        }
        return materialData.GetTextureAt(byteOffset, slot.Index()).Sample(materialData.anisoLinearWrapSampler, uv);
    }

    public float2 Uv() { return input.uv; }
    public float3 WorldPos() { return input.worldPos; }

    // The shaded face's normal, as the contract promises: a back face reads the interpolant
    // negated, matching CalculateNormal's flip on the engine's own lighting path.
    public float3 WorldNormal() { return isFrontFace ? input.normal : -input.normal; }
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
        if (S.Routed() && SlotIsRouted(inner.byteOffset, slot.Index()))
        {
            return SampleRoutedSlotBias(inner.byteOffset, slot.Index(), uv, lodBias);
        }
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

    MaterialArenaReader Reader(ForwardVSOut input, bool isFrontFace)
    {
        MaterialArenaReader reader;
        reader.byteOffset = byteOffset;
        reader.input = input;
        reader.isFrontFace = isFrontFace;
        return reader;
    }

    BiasedArenaReader BiasedReader(ForwardVSOut input, bool isFrontFace, float lodBias)
    {
        BiasedArenaReader reader;
        reader.inner = Reader(input, isFrontFace);
        reader.lodBias = lodBias;
        return reader;
    }

    public float Coverage(ForwardVSOut input, bool isFrontFace) { return G.Coverage(Reader(input, isFrontFace), params); }
    public PbrSurface Evaluate(ForwardVSOut input, bool isFrontFace) { return G.Evaluate(Reader(input, isFrontFace), params); }
    public float AlphaCutoff() { return engine.alphaCutoff; }

    public float CoverageAtBias(ForwardVSOut input, bool isFrontFace, float lodBias)
    {
        return G.Coverage(BiasedReader(input, isFrontFace, lodBias), params);
    }

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

        return ShadeSurface(input, record.Evaluate(input, isFrontFace), isFrontFace);
    }

    public float4 ShadeGameAlphaTested<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = GameRecord<G>.Load(input.materialOffset);

        if (record.Coverage(input, isFrontFace) < record.AlphaCutoff())
        {
            discard;
        }

        return ShadeSurface(input, record.Evaluate(input, isFrontFace), isFrontFace);
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
        let hashAlpha = record.CoverageAtBias(input, isFrontFace, c_HashedAlphaLodBias);
        let meanAlpha = record.CoverageAtBias(input, isFrontFace, c_HashedAlphaLodBias + c_MinifiedAlphaMeanBias);

        float alpha = SharpenMinifiedAlpha(hashAlpha, meanAlpha, input.uv, texDim);

        if (alpha < HashedAlphaThreshold(input.worldPos, alphaHashSeed))
        {
            discard;
        }

        return ShadeSurface(input, record.Evaluate(input, isFrontFace), isFrontFace);
    }

    // Premultiplied, against the transparent PSOs' SrcBlend = One; see ShadeSurfaceBlended.
    public float4 ShadeGameBlended<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = GameRecord<G>.Load(input.materialOffset);

        return ShadeSurfaceBlended(input, record.Evaluate(input, isFrontFace), isFrontFace);
    }
}
