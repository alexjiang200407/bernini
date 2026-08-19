import types.SubmeshInstance;
import forward.ForwardData;
import forward.VatData;
import forward.ViewData;
import idl.Constants;
import idl.Mesh;
import idl.Meshlet;
import idl.PsoType;
import idl.Submesh;
import idl.Clip;
import clip_playback;
import idl.VatGeom;
import idl.VatState;
import forward.common;
import forward.vertexdecode;
import debug.dbg;
import forward.ExpansionData;

// The VAT geometry stage: position and normal come from the baked texture pair by
// (column, frame) instead of the vertex bytes, which carry only the attributes a bake cannot --
// the UVs, and the connectivity the meshlets encode. Everything downstream of ForwardVSOut is the
// static path's, untouched.

ConstantBuffer<ForwardData> forwardData;
ConstantBuffer<ViewData> viewData;
ConstantBuffer<VatData> vatData;

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

float3 SampleNormal(VatGeom geom, Clip clip, uint column, float frames)
{
    uint row0, row1;
    float weight;
    FrameRows(clip, frames, row0, row1, weight);

    float3 n0 = geom.normals.Load(uint2(column, row0)).xyz * 2.0 - 1.0;
    float3 n1 = geom.normals.Load(uint2(column, row1)).xyz * 2.0 - 1.0;

    // Not renormalized: the static path hands the pixel stage an unnormalized normal too.
    return lerp(n0, n1, weight);
}

// Returns its result rather than writing through the mesh output. Slang's Metal backend crashes on
// any function that takes an OutputVertices/OutputIndices parameter, so only MSMain may index them.
ForwardVSOut EvaluateVertex(Mesh mesh, Submesh submesh, SubmeshInstance instance, Meshlet meshlet, uint v)
{
    uint vertexIdx = forwardData.vertexMapBuffer[submesh.vertexMap, meshlet.relativeVertexOffset + v];

    dbg_assert(vertexIdx < submesh.vertexCount, ErrorCode.kInvalidVertexIndex, vertexIdx, submesh.vertexCount, instance.submeshIndex);

    uint vertexByteBase = submesh.vertexData.GetStart() * 4 + vertexIdx * submesh.layout.stride;
    DecodedVertex vertex = DecodeVertex(forwardData.vertexDataBuffer, submesh.layout, vertexByteBase);

    let state = vatData.vatStateBuffer.Get(mesh.vatState);
    let geom = vatData.vatGeomBuffer.Get(state.geom);
    let clip = vatData.clipBuffer.Get(geom.clips, state.clip);

    uint column = vatData.vatColumnBuffer[geom.columnBases, instance.submeshIndex] + vertexIdx;

    // A vertex's column is always an exact texel, so the fetch is Loads by coordinate; only the
    // frame is fractional, blended across the two rows it falls between.
    float frames = ClipFrames(clip, state.phase, state.rate, viewData.time);
    float3 position = SamplePosition(geom, clip, column, frames);
    float3 normal = SampleNormal(geom, clip, column, frames);

    float4 worldPos = mul(mesh.transform, float4(position, 1.0));

    ForwardVSOut vertexOut;
    vertexOut.position = mul(viewData.viewProj, worldPos);

    // Both matrices carry their frame's sub-pixel offset; the raster keeps it and the motion vector
    // must not, so it comes back out here rather than in the pixel stage.
    vertexOut.clip = vertexOut.position;
    vertexOut.clip.xy -= viewData.jitter * vertexOut.position.w;

    // The pose the previous frame drew, through the previous frame's camera: the instance
    // transform is immutable, so re-evaluating at prevTime is the whole reprojection. Nothing
    // downstream changes -- that is ForwardVSOut's contract.
    float prevFrames = ClipFrames(clip, state.phase, state.rate, viewData.prevTime);
    float3 prevPosition = SamplePosition(geom, clip, column, prevFrames);
    float4 prevWorldPos = mul(mesh.transform, float4(prevPosition, 1.0));

    vertexOut.prevClip = mul(viewData.prevViewProj, prevWorldPos);
    vertexOut.prevClip.xy -= viewData.prevJitter * vertexOut.prevClip.w;
    vertexOut.worldPos = worldPos.xyz;
    vertexOut.normal = mul((float3x3)mesh.transform, normal);
    vertexOut.uv = vertex.uv;

    // No tangent is baked (a bind-pose basis is wrong the moment a limb rotates), so this tier
    // emits the degenerate one the pixel stage's guard turns into the geometric normal -- normal
    // maps are inert here by design.
    vertexOut.tangent = float4(0.0, 0.0, 0.0, 0.0);

    vertexOut.materialIndex = instance.material.GetOffset();
    vertexOut.materialIsLoose = 0u;

    return vertexOut;
}

uint3 EvaluatePrimitive(Submesh submesh, Meshlet meshlet, uint t)
{
    return forwardData.indexBuffer.Get<3>(submesh.indices, meshlet.relativeIndexOffset + t * 3);
}

[numthreads(cThreadsPerInstance, 1, 1)]
[shader("amplification")]
void ASMain(uint gid: SV_GroupID)
{
    uint drawInstanceIdx = expansionData.compactedInstances[ResolveInstanceBase() + gid];
    let instance = forwardData.instanceBuffer.Get(drawInstanceIdx);
    let mesh = forwardData.meshBuffer.Get(instance.meshInstance);

    let submesh = forwardData.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

    InstancedPayload payload;
    payload.instance = instance;

    DispatchMesh(submesh.GetMeshletCount(), 1, 1, payload);
}

[shader("mesh")]
[numthreads(cMeshGroupSize, 1, 1)]
[outputtopology("triangle")]
void MSMain(
    uint gid: SV_GroupID,
    uint gtid: SV_GroupThreadID,
    payload InstancedPayload payload,
    OutputVertices<ForwardVSOut, cMaxVerticesPerMeshlet> verts,
    OutputIndices<uint3, cMaxPrimsPerMeshlet> tris
)
{
    let mesh = forwardData.meshBuffer.Get(payload.instance.meshInstance);

    dbg_assert(
        payload.instance.submeshIndex < mesh.submeshes.count,
        ErrorCode.kInvalidSubmeshIndex,
        payload.instance.submeshIndex,
        mesh.submeshes.count,
        payload.instance.meshInstance.GetOffset()
    );

    let submesh = forwardData.submeshBuffer.Get(mesh.submeshes, payload.instance.submeshIndex);

    dbg_assert(
        gid < submesh.GetMeshletCount(), ErrorCode.kInvalidMeshletIndex, gid, submesh.GetMeshletCount(), payload.instance.submeshIndex
    );
    dbg_assert(submesh.layout.stride > 0, ErrorCode.kInvalidVertexLayout, submesh.layout.stride, 0, payload.instance.submeshIndex);

    let meshlet = forwardData.meshletBuffer.Get(submesh.meshlets, gid);

    dbg_assert(
        meshlet.vertexCount <= cMaxVerticesPerMeshlet, ErrorCode.kMeshletVertexOverflow, meshlet.vertexCount, cMaxVerticesPerMeshlet, gid
    );
    dbg_assert(
        meshlet.triangleCount <= cMaxPrimsPerMeshlet, ErrorCode.kMeshletPrimitiveOverflow, meshlet.triangleCount, cMaxPrimsPerMeshlet, gid
    );

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    // A meshlet holds up to cMaxVerticesPerMeshlet vertices, which the group's cMeshGroupSize threads
    // cover by striding. Never assume one thread per vertex: the two counts are unrelated.
    for (uint v = gtid; v < meshlet.vertexCount; v += cMeshGroupSize)
    {
        verts[v] = EvaluateVertex(mesh, submesh, payload.instance, meshlet, v);
    }

    // Same striding. Every triangle SetMeshOutputCounts declared must be written, or the unwritten
    // ones rasterize from undefined indices.
    for (uint t = gtid; t < meshlet.triangleCount; t += cMeshGroupSize)
    {
        tris[t] = EvaluatePrimitive(submesh, meshlet, t);
    }
}
