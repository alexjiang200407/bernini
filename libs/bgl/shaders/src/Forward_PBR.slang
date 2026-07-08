import forward.ForwardData;
import types.EntryBuffer;
import idl.PbrMaterial;
import idl.SamplerHandle;
import forward.common;
import idl.TextureHandle;
import idl.IPbrMaterial;
import forward.PbrShading;

struct MaterialData
{
    float3 cameraPos;
    EntryBuffer<PbrMaterial> pbrMaterials;

    TextureHandle irradianceMap;
    TextureHandle prefilterMap;
    TextureHandle brdfLUT;

    SamplerHandle anisioLinearWrapSampler;
    SamplerHandle linearClampSampler;
};

ConstantBuffer<MaterialData> materialData : register(b0, space1);

// The optimized, baked PBR material: a three-texture triplet (base color, normal, ORM). Conforms to
// IPbrMaterial so the generic pixel shader can shade it. `Load` reads its own bindless buffer field
// by slot index, so nothing is reinterpreted. Factors are folded into the resolved channels here.
extension PbrMaterial : IPbrMaterial
{
    static PbrMaterial Load(uint index)
    {
        return materialData.pbrMaterials[index];
    }

    float4 GetBaseColor(SamplerHandle s, float2 uv)
    {
        return baseColorTexture.Sample(s, uv) * baseColorFactor;
    }

    float3 GetORM(SamplerHandle s, float2 uv)
    {
        return ormTexture.Sample(s, uv).rgb * float3(1.0, roughnessFactor, metallicFactor);
    }

    float2 GetNormalXY(SamplerHandle s, float2 uv)
    {
        return normalTexture.Sample(s, uv).xy;
    }
}

// Generic shading core: resolve the material via static polymorphism (M.Load), then run the shared
// lighting path. Each concrete M compiles to its own specialization -- no runtime material branch.
float4 Shade<M : IPbrMaterial>(ForwardVSOut input)
{
    let wrapSampler = materialData.anisioLinearWrapSampler;

    M material = M.Load(input.materialIndex);

    return ShadeSurface(
        input,
        material.GetBaseColor(wrapSampler, input.uv),
        material.GetORM(wrapSampler, input.uv),
        material.GetNormalXY(wrapSampler, input.uv),
        materialData.cameraPos,
        materialData.irradianceMap,
        materialData.prefilterMap,
        materialData.brdfLUT,
        materialData.linearClampSampler
    );
}

[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    return Shade<PbrMaterial>(input);
}
