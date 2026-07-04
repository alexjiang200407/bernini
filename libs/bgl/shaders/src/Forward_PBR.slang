import forward.ForwardData;
import types.EntryBuffer;
import idl.PbrMaterial;
import forward.common;

struct MaterialData
{
    EntryBuffer<PbrMaterial> pbrMaterials;

    Sampler2D.Handle anisioLinearWrapSampler;
    Sampler2D.Handle linearClampSampler;
};

ConstantBuffer<MaterialData> materialData;

[shader("pixel")]
float4 PSMain(ForwardVSOut input) : SV_Target
{
    let material = materialData.pbrMaterials[input.materialIndex];
    return float4(1.0, 0.0, 0.0, 1.0);
}
