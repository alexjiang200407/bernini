import bgl.LitSurfaceSource;
import bgl.MaterialReader;
import bgl.PbrSurface;
import bgl.SurfaceLight;
import bgl.SurfaceSource;
import idl.GameSurfaceRecord;
import idl.IMaterial;
import idl.RawEntry;
import lib.forward.MaterialData;
import lib.forward.MaterialShading;
import lib.forward.common;
import lib.geom.vertexdecode;
import lib.math.HashedAlpha;
import lib.math.PbrShading;
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
// the reason that reader is a wrapper: a bias parameter here would put SampleBias on every
// program's material sampling path to serve the coverage read.
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
    public float2 Uv1() { return input.uv1; }
    public bool HasUv1() { return HasUv1(input.uv1); }
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
// sampling path of every program to serve one. The interpolants forward, so there is one definition of
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
    public float2 Uv1() { return inner.Uv1(); }
    public bool HasUv1() { return inner.HasUv1(); }
    public float3 WorldPos() { return inner.WorldPos(); }
    public float3 WorldNormal() { return inner.WorldNormal(); }
    public float4 WorldTangent() { return inner.WorldTangent(); }
    public float3 CameraPos() { return inner.CameraPos(); }
}

// The readers a record hands its surface, built by the record's location alone: neither depends
// on the surface's type, so both record structs below share these.
MaterialArenaReader ArenaReaderAt(uint byteOffset, ForwardVSOut input, bool isFrontFace)
{
    MaterialArenaReader reader;
    reader.byteOffset = byteOffset;
    reader.input = input;
    reader.isFrontFace = isFrontFace;
    return reader;
}

BiasedArenaReader BiasedArenaReaderAt(uint byteOffset, ForwardVSOut input, bool isFrontFace, float lodBias)
{
    BiasedArenaReader reader;
    reader.inner = ArenaReaderAt(byteOffset, input, isFrontFace);
    reader.lodBias = lodBias;
    return reader;
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

    MaterialArenaReader Reader(ForwardVSOut input, bool isFrontFace) { return ArenaReaderAt(byteOffset, input, isFrontFace); }

    BiasedArenaReader BiasedReader(ForwardVSOut input, bool isFrontFace, float lodBias)
    {
        return BiasedArenaReaderAt(byteOffset, input, isFrontFace, lodBias);
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

// The engine's side of ISurfaceLight: the same sun and environment the PBR lighting path reads,
// world-space directions carried through the environment's rotation before the cubes are sampled.
// An unset environment samples the same null handles the PBR path samples, so "black when no
// lighting is set" is one behaviour, not two.
public struct MaterialSurfaceLight : ISurfaceLight
{
    public float3 SunDirection() { return materialData.sunDirection; }
    public float3 SunRadiance() { return materialData.sunRadiance; }

    public float3 Irradiance(float3 dir) { return materialData.GetSurfaceEnv().Irradiance(mul(float3x3(materialData.envRotation), dir)); }

    // The contract's roughness maps onto the prefilter chain's highest mip index directly. The
    // PBR path's screen-space-footprint floor (PbrShading.slang) is deliberately not carried
    // over: it needs the reflection vector's derivatives, which this seam does not have -- a lit
    // surface asking for a sharp reflection on a small distant object inherits the grain that
    // floor guards against.
    public float3 Reflection(float3 dir, float roughness)
    {
        return materialData.GetSurfaceEnv().Prefiltered(
            mul(float3x3(materialData.envRotation), dir), saturate(roughness) * c_PrefilterMaxLod
        );
    }
}

// A lit surface's record, loaded exactly as GameRecord loads a PBR surface's: the engine's fixed
// part is shared, and only the function the params are handed to differs.
public struct LitGameRecord<G : ILitSurfaceSource>
{
    GameSurfaceRecord engine;
    G.MaterialParams params;
    uint byteOffset;

    public static LitGameRecord<G> Load(uint byteOffset)
    {
        LitGameRecord<G> record;
        record.engine = materialData.materials.LoadRecordAs<GameSurfaceRecord>(RawEntry<IMaterial>.At(byteOffset));
        record.params = materialData.materials.LoadAt<G.MaterialParams>(byteOffset + cGameSurfaceParamsByteOffset);
        record.byteOffset = byteOffset;
        return record;
    }

    MaterialArenaReader Reader(ForwardVSOut input, bool isFrontFace) { return ArenaReaderAt(byteOffset, input, isFrontFace); }

    BiasedArenaReader BiasedReader(ForwardVSOut input, bool isFrontFace, float lodBias)
    {
        return BiasedArenaReaderAt(byteOffset, input, isFrontFace, lodBias);
    }

    public float Coverage(ForwardVSOut input, bool isFrontFace) { return G.Coverage(Reader(input, isFrontFace), params); }
    public float4 Shade(ForwardVSOut input, bool isFrontFace)
    {
        return G.Shade(Reader(input, isFrontFace), MaterialSurfaceLight(), params);
    }
    public float AlphaCutoff() { return engine.alphaCutoff; }

    public float CoverageAtBias(ForwardVSOut input, bool isFrontFace, float lodBias)
    {
        return G.Coverage(BiasedReader(input, isFrontFace, lodBias), params);
    }

    // @pre engine.coverageSlot != cNoCoverageSlot, as GameRecord::CoverageDimensions.
    public float2 CoverageDimensions() { return materialData.GetTextureAt(byteOffset, engine.coverageSlot).GetDimensions(); }
}

public struct NullSurfaceParams
{
    public uint unused;
};

// What the build validates the game-surface programs against: a flat mid-grey dielectric, fully
// covering. A registered surface's programs exist only at runtime (surface_registry.cpp generates
// them), so programs/forward/GameSurfaceShapes.slang instantiates every shape on this instead, and
// that is what holds the shapes to DXIL validation. Nothing draws it.
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

public struct NullLitSurfaceParams
{
    public uint unused;
};

// NullSurface's lit twin: what holds the lit shapes to DXIL validation
// (programs/forward/GameSurfaceShapes.slang). Nothing draws it.
public struct NullLitSurface : ILitSurfaceSource
{
    public typealias MaterialParams = NullLitSurfaceParams;

    public static float Coverage<R : IMaterialReader>(R reader, NullLitSurfaceParams params) { return 1.0; }

    public static float4 Shade<R : IMaterialReader, L : ISurfaceLight>(R reader, L light, NullLitSurfaceParams params)
    {
        return float4(0.5, 0.5, 0.5, 1.0);
    }
}

public extension MaterialData
{
    // The colour-pass programs a registered surface draws through, one per layer; each generated
    // program (surface_registry.cpp) is a one-line call to one of these.
    public ForwardPSOut GameOpaqueProgram<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        ForwardPSOut output;
        output.color = ShadeGame<G>(input, isFrontFace);
        output.motion = ComputeMotionVector(input);
        return output;
    }

    public ForwardPSOut GameAlphaTestedProgram<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        ForwardPSOut output;
        output.color = ShadeGameAlphaTested<G>(input, isFrontFace);
        output.motion = ComputeMotionVector(input);
        return output;
    }

    public ForwardPSOut GameHashedAlphaProgram<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        ForwardPSOut output;
        output.color = ShadeGameHashedAlpha<G>(input, isFrontFace);
        output.motion = ComputeMotionVector(input);
        return output;
    }

    public float4 ShadeGame<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = GameRecord<G>.Load(input.materialOffset);

        return float4(ShadeSurface(input, record.Evaluate(input, isFrontFace), isFrontFace), 1.0);
    }

    // The one cutout coverage decision for a surface; the receiver pass runs it with no shading
    // behind it, as MaterialShading's DiscardUncoveredAlphaTested for a PBR record.
    public void DiscardUncoveredGameAlphaTested<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = GameRecord<G>.Load(input.materialOffset);

        if (record.Coverage(input, isFrontFace) < record.AlphaCutoff())
        {
            discard;
        }
    }

    // The one hashed coverage decision for a surface; see ShadeGameHashedAlpha for the scheme.
    public void DiscardUncoveredGameHashedAlpha<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
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
    }

    public float4 ShadeGameAlphaTested<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        DiscardUncoveredGameAlphaTested<G>(input, isFrontFace);

        let record = GameRecord<G>.Load(input.materialOffset);

        return float4(ShadeSurface(input, record.Evaluate(input, isFrontFace), isFrontFace), 1.0);
    }

    // Stochastic coverage in place of a cutoff, as ShadeHashedAlpha does it for a PBR record: the
    // fragment survives with probability equal to its coverage and writes real depth, so this is the
    // blend only as the ensemble over pixels and frames, and only with temporal AA running.
    //
    // The surface's cutoff is deliberately unused, for the reason the PBR path leaves it alone: a
    // cutoff is the thing being replaced.
    public float4 ShadeGameHashedAlpha<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        DiscardUncoveredGameHashedAlpha<G>(input, isFrontFace);

        let record = GameRecord<G>.Load(input.materialOffset);

        return float4(ShadeSurface(input, record.Evaluate(input, isFrontFace), isFrontFace), 0.0);
    }

    // Premultiplied, against the shared blend PSO's SrcBlend = One; see ShadeSurfaceBlended.
    public float4 ShadeGameBlended<G : ISurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = GameRecord<G>.Load(input.materialOffset);

        return ShadeSurfaceBlended(input, record.Evaluate(input, isFrontFace), isFrontFace);
    }

    // The lit twins of the programs above: a lit surface's Shade is its whole lighting, so these
    // never call ShadeSurface. Exposure is applied here and nowhere the surface can see
    // (bgl/LitSurfaceSource.slang), so everything after the forward pass keeps one meaning.
    public ForwardPSOut GameLitOpaqueProgram<G : ILitSurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        ForwardPSOut output;
        output.color = ShadeGameLit<G>(input, isFrontFace);
        output.motion = ComputeMotionVector(input);
        return output;
    }

    public ForwardPSOut GameLitAlphaTestedProgram<G : ILitSurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        ForwardPSOut output;
        output.color = ShadeGameLitAlphaTested<G>(input, isFrontFace);
        output.motion = ComputeMotionVector(input);
        return output;
    }

    public ForwardPSOut GameLitHashedAlphaProgram<G : ILitSurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        ForwardPSOut output;
        output.color = ShadeGameLitHashedAlpha<G>(input, isFrontFace);
        output.motion = ComputeMotionVector(input);
        return output;
    }

    public float4 ShadeGameLit<G : ILitSurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = LitGameRecord<G>.Load(input.materialOffset);

        return float4(record.Shade(input, isFrontFace).rgb * exposure, 1.0);
    }

    // The one cutout coverage decision for a lit surface, as DiscardUncoveredGameAlphaTested.
    public void DiscardUncoveredGameLitAlphaTested<G : ILitSurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = LitGameRecord<G>.Load(input.materialOffset);

        if (record.Coverage(input, isFrontFace) < record.AlphaCutoff())
        {
            discard;
        }
    }

    // The one hashed coverage decision for a lit surface; the two-level scheme is
    // DiscardUncoveredGameHashedAlpha's, unchanged by who lights the pixel.
    public void DiscardUncoveredGameLitHashedAlpha<G : ILitSurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = LitGameRecord<G>.Load(input.materialOffset);

        let texDim = record.CoverageDimensions() * exp2(c_HashedAlphaLodBias);
        let hashAlpha = record.CoverageAtBias(input, isFrontFace, c_HashedAlphaLodBias);
        let meanAlpha = record.CoverageAtBias(input, isFrontFace, c_HashedAlphaLodBias + c_MinifiedAlphaMeanBias);

        float alpha = SharpenMinifiedAlpha(hashAlpha, meanAlpha, input.uv, texDim);

        if (alpha < HashedAlphaThreshold(input.worldPos, alphaHashSeed))
        {
            discard;
        }
    }

    public float4 ShadeGameLitAlphaTested<G : ILitSurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        DiscardUncoveredGameLitAlphaTested<G>(input, isFrontFace);

        let record = LitGameRecord<G>.Load(input.materialOffset);

        return float4(record.Shade(input, isFrontFace).rgb * exposure, 1.0);
    }

    public float4 ShadeGameLitHashedAlpha<G : ILitSurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        DiscardUncoveredGameLitHashedAlpha<G>(input, isFrontFace);

        let record = LitGameRecord<G>.Load(input.materialOffset);

        return float4(record.Shade(input, isFrontFace).rgb * exposure, 0.0);
    }

    // Premultiplied against the shared blend PSO's SrcBlend = One. Shade's a is the surface's
    // coverage, and a lit surface has no transmission split: everything it returns thins by
    // coverage whole, which is the alpha = 0 arm of BlendedSurface.
    public float4 ShadeGameLitBlended<G : ILitSurfaceSource>(ForwardVSOut input, bool isFrontFace)
    {
        let record = LitGameRecord<G>.Load(input.materialOffset);

        let shaded = record.Shade(input, isFrontFace);
        let coverage = saturate(shaded.a);
        return float4(shaded.rgb * exposure * coverage, coverage);
    }
}
