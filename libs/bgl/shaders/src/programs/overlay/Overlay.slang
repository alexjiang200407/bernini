import idl.OverlayVertex;
import lib.types.UploadBuffer;

struct OverlayDrawData
{
    UploadBuffer<OverlayVertex> vertices;
    UploadBuffer<uint> indices;
    Texture2D.Handle texture;
    SamplerState.Handle sampler;

    // Applied to the translated pixel position; carries a w so a projective transform survives.
    float4x4 transform;
    float2 translation;
    float2 targetSize;
    uint triangleCount;
};

ConstantBuffer<OverlayDrawData> gOverlayDraw;

// Tracks c_TrianglesPerGroup in OverlayPass.cpp. Three unshared vertices per triangle keeps a group
// under the 256-vertex output cap without deduplicating indices.
static const uint c_TrianglesPerGroup = 64;

struct VSOut
{
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 uv : TEXCOORD;
};

float3 SrgbToLinear(float3 c)
{
    const float3 low = c / 12.92;
    const float3 high = pow((c + 0.055) / 1.055, float3(2.4, 2.4, 2.4));
    return lerp(low, high, step(0.04045, c));
}

// The vertex carries sRGB bytes premultiplied by alpha. Premultiplication is only valid in the space
// the blend runs in, so the alpha is divided out before the decode and multiplied back after it:
// decoding the premultiplied value as-is would weight a half-covered edge by 0.5^2.2.
float4 DecodeColor(uint packed)
{
    const float4 encoded = float4(packed & 0xFFu, (packed >> 8) & 0xFFu, (packed >> 16) & 0xFFu, packed >> 24) / 255.0;
    const float3 straight = encoded.a > 0.0 ? encoded.rgb / encoded.a : float3(0.0, 0.0, 0.0);
    return float4(SrgbToLinear(straight) * encoded.a, encoded.a);
}

[shader("mesh")]
[numthreads(64, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gid: SV_GroupID, uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 192> verts, OutputIndices<uint3, 64> tris)
{
    const uint first = gid * c_TrianglesPerGroup;
    const uint count = min(c_TrianglesPerGroup, gOverlayDraw.triangleCount - first);

    SetMeshOutputCounts(count * 3, count);

    if (gtid < count)
    {
        const uint triangle = first + gtid;

        [unroll]
        for (uint k = 0; k < 3; ++k)
        {
            const uint index = gOverlayDraw.indices[triangle * 3 + k];
            const OverlayVertex vertex = gOverlayDraw.vertices[index];

            const float4 pixel = mul(gOverlayDraw.transform, float4(vertex.position + gOverlayDraw.translation, 0.0, 1.0));

            // Pixel space to clip space with w kept: x / w * 2 / width - 1, folded so the divide
            // happens once in the rasterizer.
            VSOut vout;
            vout.position = float4(
                pixel.x * 2.0 / gOverlayDraw.targetSize.x - pixel.w, pixel.w - pixel.y * 2.0 / gOverlayDraw.targetSize.y, 0.0, pixel.w
            );
            vout.color = DecodeColor(vertex.color);
            vout.uv = vertex.uv;

            verts[gtid * 3 + k] = vout;
        }

        tris[gtid] = uint3(gtid * 3, gtid * 3 + 1, gtid * 3 + 2);
    }
}

// The texture is straight alpha and, through an sRGB view, already linear; the color arrives
// premultiplied linear. The product is premultiplied linear, which is what the blend state expects.
[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    const float4 texel = gOverlayDraw.texture.SampleLevel(gOverlayDraw.sampler, input.uv, 0.0);
    return float4(texel.rgb * input.color.rgb * texel.a, input.color.a * texel.a);
}
