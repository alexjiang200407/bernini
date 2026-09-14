import idl.BlobShadow;
import idl.MeshInstance;
import lib.types.EntryBuffer;
import lib.types.UploadBuffer;

// One workgroup draws one blob-shadow disc: a quad flattened onto the ground plane under its
// placement, darkened by a radial falloff in the pixel shader. Composites between the opaque
// buckets and the depth-sorted transparents, colour only -- a blended disc has no single depth to
// reproject, exactly as programs.forward.Transparent has none.

struct Uniforms
{
    UploadBuffer<BlobShadow> blobBuffer;
    EntryBuffer<MeshInstance> meshBuffer;

    float4x4 viewProj;

    // The scene's ground plane, world space, normal unit length: what every disc flattens onto.
    float3 groundPoint;
    float3 groundNormal;
};

ConstantBuffer<Uniforms> gBlobShadowData;

// Lifts the disc off coplanar ground geometry, which would otherwise depth-fight it.
static const float cGroundBias = 0.01;

// How far the disc shrinks by the time it has fully faded: an airborne placement's disc tightens
// toward half its grounded radius as the contact softens away.
static const float cLiftShrink = 0.5;

struct VSOut
{
    float4 position : SV_Position;
    float2 disc : DISC; // [-1, 1] across the quad; length 1 is the rim
    float alpha : ALPHA;
};

[shader("mesh")]
[numthreads(4, 1, 1)]
[outputtopology("triangle")]
void MSMain(uint gid: SV_GroupID, uint gtid: SV_GroupThreadID, OutputVertices<VSOut, 4> verts, OutputIndices<uint3, 2> tris)
{
    let blob = gBlobShadowData.blobBuffer[gid];
    let mesh = gBlobShadowData.meshBuffer[blob.mesh];
    let n = gBlobShadowData.groundNormal;

    let position = float3(mesh.transform[0].w, mesh.transform[1].w, mesh.transform[2].w);
    let scale = length(float3(mesh.transform[0].x, mesh.transform[1].x, mesh.transform[2].x));

    let height = dot(position - gBlobShadowData.groundPoint, n);
    let fade = saturate(1.0 - height / blob.fadeHeight);

    let emitted = fade > 0.0 ? 4u : 0u;
    SetMeshOutputCounts(emitted, emitted / 2);
    if (emitted == 0)
    {
        return;
    }

    let center = position - n * height + n * cGroundBias;
    let radius = blob.radius * scale * lerp(cLiftShrink, 1.0, fade);

    let axis = abs(n.x) < 0.7 ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 1.0);
    let tangent = normalize(cross(n, axis));
    let bitangent = cross(n, tangent);

    if (gtid < 4)
    {
        let disc = float2((gtid & 1) != 0 ? 1.0 : -1.0, (gtid & 2) != 0 ? 1.0 : -1.0);
        let world = center + (tangent * disc.x + bitangent * disc.y) * radius;

        VSOut vout;
        vout.position = mul(gBlobShadowData.viewProj, float4(world, 1.0));
        vout.disc = disc;
        vout.alpha = blob.intensity * fade;
        verts[gtid] = vout;
    }

    if (gtid == 0)
    {
        tris[0] = uint3(0, 1, 2);
        tris[1] = uint3(2, 1, 3);
    }
}

[shader("pixel")]
float4 PSMain(VSOut input) : SV_Target
{
    let falloff = saturate(1.0 - dot(input.disc, input.disc));

    // Premultiplied black: the One/InvSrcAlpha blend scales the scene by 1 - alpha, so the disc
    // only darkens. Squared for a soft shoulder at the rim.
    return float4(0.0, 0.0, 0.0, input.alpha * falloff * falloff);
}
