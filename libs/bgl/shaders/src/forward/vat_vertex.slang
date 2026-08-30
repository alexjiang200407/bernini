// The VAT tier's vertex evaluation: position and normal fetched from the baked texture pair by
// (column, frame) rather than from the vertex bytes.
//
// One of three; forward/mesh_stage.slang holds what they share and says why the split exists. Two
// entry points call this one: Forward_VatMesh, which draws the PSO-bucketed phase, and
// Forward_AnyMesh, which draws lists that mix tiers.

import types.SubmeshInstance;
import forward.VatData;
import forward.common;
import forward.mesh_stage;
import forward.vertexdecode;
import idl.Clip;
import idl.VatGeom;
import idl.VatState;
import clip_playback;

public ConstantBuffer<VatData> vatData;

// The rows a fractional frame falls between. The pad row after each clip exists so a clip never
// bleeds into the one stacked below; ClipFrameIndices tops out at the clip's last real row, so the
// pad is never reached -- on a loop that row repeats frame 0, which is what makes the crossing
// seamless.
void FrameRows(Clip clip, float frames, out uint row0, out uint row1, out float weight)
{
    uint f0;
    uint f1;
    ClipFrameIndices(clip, frames, f0, f1, weight);

    row0 = clip.firstFrame + f0;
    row1 = clip.firstFrame + f1;
}

float3 SamplePosition(VatGeom geom, Clip clip, uint column, float frames)
{
    uint row0, row1;
    float weight;
    FrameRows(clip, frames, row0, row1, weight);

    float3 packed0 = geom.positions.Load(uint2(column, row0)).xyz;
    float3 packed1 = geom.positions.Load(uint2(column, row1)).xyz;
    float3 packed = lerp(packed0, packed1, weight);

    return geom.boundsMin.xyz + packed * geom.boundsExtent.xyz;
}

// xyz the object-space normal, w the tangent's twist in radians -- both as the bake packed them
// (see docs/vat.md). The normal is not renormalized: the static path hands the pixel stage an
// unnormalized normal too.
float4 SampleNormalTwist(VatGeom geom, Clip clip, uint column, float frames)
{
    uint row0, row1;
    float weight;
    FrameRows(clip, frames, row0, row1, weight);

    float4 t0 = geom.normals.Load(uint2(column, row0));
    float4 t1 = geom.normals.Load(uint2(column, row1));
    float3 normal = lerp(t0.xyz, t1.xyz, weight) * 2.0 - 1.0;

    // The twist is an angle, so two frames whose twists straddle +-pi blend the short way round
    // rather than sweeping through zero.
    float turns = t1.w - t0.w;
    turns -= round(turns);
    float twist = (t0.w - 0.5 + weight * turns) * 2.0 * 3.14159265359;

    return float4(normal, twist);
}

// `v` carried by the shortest arc from unit `from` onto unit `to`. Antiparallel directions have no
// shortest arc and leave `v` as it is; the bake (assetlib's vat_tangent.h) makes the same choice,
// which is what keeps the twist it measured exact here.
float3 RotateByShortestArc(float3 v, float3 from, float3 to)
{
    float c = dot(from, to);
    if (c < -1.0 + 1e-4)
    {
        return v;
    }
    float3 axis = cross(from, to);
    return v * c + cross(axis, v) + axis * (dot(axis, v) / (1.0 + c));
}

// The posed tangent: the bind tangent carried onto the posed normal, then turned about it by the
// baked twist. A submesh with no tangent or no normal yields the degenerate tangent the pixel
// stage's guard reads as "shade with the geometric normal".
float3 PosedTangent(float3 bindNormal, float3 bindTangent, float3 posedNormal, float twist)
{
    float bindLen2 = dot(bindNormal, bindNormal);
    float posedLen2 = dot(posedNormal, posedNormal);
    if (bindLen2 < 1e-8 || posedLen2 < 1e-8)
    {
        return float3(0.0, 0.0, 0.0);
    }

    float3 n = posedNormal * rsqrt(posedLen2);
    float3 carried = RotateByShortestArc(bindTangent, bindNormal * rsqrt(bindLen2), n);
    return carried * cos(twist) + cross(n, carried) * sin(twist);
}

public ForwardVSOut VatVertex(MeshletContext ctx, SubmeshInstance instance, uint v)
{
    uint vertexIdx;
    DecodedVertex vertex = DecodeMeshletVertex(ctx, instance, v, vertexIdx);

    let state = vatData.vatStateBuffer.Get(ctx.mesh.vatState);
    let geom = vatData.vatGeomBuffer.Get(state.geom);
    let clip = vatData.clipBuffer.Get(geom.clips, state.clip);

    uint column = vatData.vatColumnBuffer[geom.columnBases, instance.submeshIndex] + vertexIdx;

    // A vertex's column is always an exact texel, so the fetch is Loads by coordinate; only the
    // frame is fractional, blended across the two rows it falls between.
    float frames = ClipFrames(clip, state.phase, state.rate, viewData.time);
    float3 position = SamplePosition(geom, clip, column, frames);
    float4 normalTwist = SampleNormalTwist(geom, clip, column, frames);
    float3 normal = normalTwist.xyz;
    float3 tangent = PosedTangent(vertex.normal, vertex.tangent.xyz, normal, normalTwist.w);

    float4 worldPos = mul(ctx.mesh.transform, float4(position, 1.0));

    // The pose the previous frame drew: the instance transform is immutable, so re-evaluating at
    // prevTime is the whole reprojection.
    float prevFrames = ClipFrames(clip, state.phase, state.rate, viewData.prevTime);
    float3 prevPosition = SamplePosition(geom, clip, column, prevFrames);
    float4 prevWorldPos = mul(ctx.mesh.transform, float4(prevPosition, 1.0));

    ForwardVSOut vertexOut;
    ProjectVertex(vertexOut, worldPos, prevWorldPos);

    vertexOut.normal = mul((float3x3)ctx.mesh.transform, normal);
    vertexOut.uv = vertex.uv;
    vertexOut.tangent = float4(mul((float3x3)ctx.mesh.transform, tangent), vertex.tangent.w);

    vertexOut.materialOffset = instance.material.GetByteOffset();

    return vertexOut;
}
