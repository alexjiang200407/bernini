import idl.Constants;
import idl.IAlphaTestMaterial;
import idl.IPbrMaterial;
import idl.ISurfaceEnv;
import lib.forward.common;
import lib.forward.MaterialData;
import lib.math.PbrShading;
import lib.math.HashedAlpha;

public extension MaterialData
{
    // The shared tail of both shading paths. Base color is passed in rather than sampled here, so the
    // alpha-tested path can test it without sampling the texture a second time.
    SurfaceLobes EvaluateWithBaseColor<M : IPbrMaterial>(ForwardVSOut input, M material, float4 baseColor, bool isFrontFace)
    {
        return EvaluateSurface(
            GetSurfaceEnv(),
            GetSurfaceParams(),
            input.worldPos,
            input.normal,
            input.tangent,
            baseColor,
            material.GetORM(input.uv),
            material.GetNormalXY(input.uv),
            material.GetSpecular(),
            isFrontFace
        );
    }

    float4 ShadeWithBaseColor<M : IPbrMaterial>(ForwardVSOut input, M material, float4 baseColor, bool isFrontFace)
    {
        let lobes = EvaluateWithBaseColor(input, material, baseColor, isFrontFace);

        return float4(lobes.diffuse + lobes.specular, baseColor.a);
    }

    public float4 Shade<M : IPbrMaterial>(ForwardVSOut input, bool isFrontFace)
    {
        M material = M.Load(input.materialOffset);

        return ShadeWithBaseColor(input, material, material.GetBaseColor(input.uv), isFrontFace);
    }

    /**
     * A blended surface, as premultiplied colour and the coverage that goes with it.
     *
     * The returned colour is already weighted by its own coverage, which is correct only against the
     * transparent PSOs' SrcBlend = One (ForwardPass's c_Psos): either side changed alone is silently
     * wrong rather than a build error. What the transmission factor selects between, and why coverage
     * rises with the reflectance, is in docs/passes.md#blended-surfaces.
     */
    public float4 ShadeBlended<M : IPbrMaterial>(ForwardVSOut input, bool isFrontFace)
    {
        M material = M.Load(input.materialOffset);

        let baseColor = material.GetBaseColor(input.uv);
        let lobes = EvaluateWithBaseColor(input, material, baseColor, isFrontFace);

        let alpha = baseColor.a;
        let transmission = saturate(material.GetTransmission());

        let reflected = max(max(lobes.reflectance.r, lobes.reflectance.g), lobes.reflectance.b);

        let specularWeight = lerp(alpha, 1.0, transmission);
        let coverage = lerp(alpha, alpha + (1.0 - alpha) * reflected, transmission);

        return float4(lobes.diffuse * alpha + lobes.specular * specularWeight, saturate(coverage));
    }

    public float4 ShadeAlphaTested<M : IPbrMaterial & IAlphaTestMaterial>(ForwardVSOut input, bool isFrontFace)
    {
        M material = M.Load(input.materialOffset);

        let baseColor = material.GetBaseColor(input.uv);

        if (baseColor.a < material.GetAlphaCutoff())
        {
            discard;
        }

        return ShadeWithBaseColor(input, material, baseColor, isFrontFace);
    }

    // Stochastic coverage in place of a cutoff: the fragment survives with probability equal to its
    // alpha, and writes real depth like any opaque surface. One frame of it is noise; the ensemble over
    // pixels and frames is the blend, which is why this pairs with temporal AA and not without it.
    //
    // The material's alphaCutoff is deliberately unused. A cutoff is the thing being replaced, and
    // applying both would put back the binary silhouette that costs the surface its layers.
    public float4 ShadeHashedAlpha<M : IPbrMaterial & IAlphaTestMaterial>(ForwardVSOut input, bool isFrontFace)
    {
        M material = M.Load(input.materialOffset);

        let baseColor = material.GetBaseColor(input.uv);

        // At minification the sampled alpha is many strands averaged, and coverage that honestly
        // matches it is a haze the backdrop swallows. The alpha driving the hash is read one level
        // finer than the footprint, where a sub-texel strand still has shape for the steepening to
        // work against; the mean it steepens about stays one octave coarser than that read, and the
        // sharpening measures its minification in the sampled level's own texels -- the footprint's
        // would be an octave too steep and saturate diluted strands past their area.
        let texDim = material.GetBaseColorDimensions() * exp2(c_HashedAlphaLodBias);
        let hashAlpha = material.GetBaseColorAlphaBiased(input.uv, c_HashedAlphaLodBias);
        let meanAlpha = material.GetBaseColorAlphaBiased(input.uv, c_HashedAlphaLodBias + c_MinifiedAlphaMeanBias);

        float alpha = SharpenMinifiedAlpha(hashAlpha, meanAlpha, input.uv, texDim);

        if (alpha < HashedAlphaThreshold(input.worldPos, alphaHashSeed))
        {
            discard;
        }

        return ShadeWithBaseColor(input, material, baseColor, isFrontFace);
    }
}
