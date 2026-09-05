// Everything the forward amplification and mesh stages do that does not depend on how a vertex is
// posed: which instance a dispatch covers and how many meshlets it launches, which meshlet a group
// draws, where its triangles and its bind-pose vertex bytes come from, and the reprojection through
// both cameras. A tier supplies only its vertex evaluation on top of this.
//
// It is a module and not the top of one shader because four entry points share it -- one per tier
// plus the tier-branching programs/forward/AnyMesh -- and before the split all of this was copied
// into three
// of them.

import idl.Constants;
import idl.MeshInstance;
import idl.Meshlet;
import idl.Submesh;
import idl.VertexLayout;
import lib.debug.dbg;
import lib.forward.common;
import lib.forward.ExpansionData;
import lib.forward.ForwardData;
import lib.forward.MaterialData;
import lib.geom.vertexdecode;
import lib.data.ViewData;
import lib.data.SubmeshInstance;

public ConstantBuffer<ForwardData> forwardData;
public ConstantBuffer<ViewData> viewData;

// The tables one mesh-shader group draws from, resolved once per group rather than per vertex.
public struct MeshletContext
{
    public MeshInstance mesh;
    public Submesh submesh;
    public Meshlet meshlet;

    // Whether this group's back-facing triangles are the mesh stage's to cull: the draw says its
    // pipeline draws both sides, and the material says it wants one. Decided once per group.
    public bool cull;
};

// Whether the draw in flight leaves back faces to the mesh stage at all. A cbuffer value, so a
// barrier gated on it sits in uniform control flow on every backend.
public bool DrawCullsBackfaces()
{
    return expansionData.cullBackfaces != 0;
}

// Each vertex's clip position, kept for the primitive loop: the output array cannot be read back,
// and a facing test wants all three corners of a triangle at once.
groupshared float3 gClipXYW[cMaxVerticesPerMeshlet];

public void RecordClip(uint v, float4 clip)
{
    gClipXYW[v] = clip.xyw;
}

// The triangle to emit for `tri`: itself, or a degenerate one the rasterizer sets up and drops
// without walking a single tile -- which is the whole point, since a long thin card triangle costs
// the rasterizer by its length whether or not a pixel shader ever runs. Taken in homogeneous clip
// space so a triangle crossing the near plane is judged like the hardware judges it, and the sign
// is the one FrontCounterClockwise names on the screen.
//
// @pre when ctx.cull, every vertex the triangle names has passed through RecordClip and the group
// has synchronised since.
public uint3 CullBackface(MeshletContext ctx, uint3 tri)
{
    if (!ctx.cull)
    {
        return tri;
    }

    float3 p0 = gClipXYW[tri.x];
    float3 p1 = gClipXYW[tri.y];
    float3 p2 = gClipXYW[tri.z];

    float area = (p1.x * p2.y - p2.x * p1.y) * p0.z + (p2.x * p0.y - p0.x * p2.y) * p1.z + (p0.x * p1.y - p1.x * p0.y) * p2.z;

    return area > 0.0 ? tri : uint3(tri.x, tri.x, tri.x);
}

// The instance this amplification group covers, and how many meshlets it dispatches. Each instance
// is one submesh, so only that submesh's meshlets are dispatched.
public uint ResolveDispatch(uint gid, out InstancedPayload payload)
{
    uint drawInstanceIdx = expansionData.compactedInstances[ResolveInstanceBase() + gid];
    let instance = forwardData.instanceBuffer.Get(drawInstanceIdx);
    let mesh = forwardData.meshBuffer.Get(instance.meshInstance);
    let submesh = forwardData.submeshBuffer.Get(mesh.submeshes, instance.submeshIndex);

    payload.instance = instance;

    return submesh.GetMeshletCount();
}

public MeshletContext PrepareMeshlet(SubmeshInstance instance, uint gid)
{
    MeshletContext ctx;
    ctx.mesh = forwardData.meshBuffer.Get(instance.meshInstance);

    dbg_assert(
        instance.submeshIndex < ctx.mesh.submeshes.count,
        ErrorCode.kInvalidSubmeshIndex,
        instance.submeshIndex,
        ctx.mesh.submeshes.count,
        instance.meshInstance.GetOffset()
    );

    ctx.submesh = forwardData.submeshBuffer.Get(ctx.mesh.submeshes, instance.submeshIndex);

    dbg_assert(
        gid < ctx.submesh.GetMeshletCount(), ErrorCode.kInvalidMeshletIndex, gid, ctx.submesh.GetMeshletCount(), instance.submeshIndex
    );
    dbg_assert(ctx.submesh.layout.stride > 0, ErrorCode.kInvalidVertexLayout, ctx.submesh.layout.stride, 0, instance.submeshIndex);
    // Position is the only attribute a mesh cannot render without; every other decodes to a default.
    // A property of the layout, so checked once per group here rather than once per vertex.
    bool hasPosition = false;
    for (uint i = 0; i < ctx.submesh.layout.attributeCount; ++i)
    {
        hasPosition = hasPosition || ctx.submesh.layout.attributes[i].semantic == VertexSemantic.kPosition;
    }
    dbg_assert(hasPosition, ErrorCode.kInvalidVertexLayout, 0, 0, instance.submeshIndex);

    // A submesh with meshlets has vertices. Only the arena's first 16 bytes are the reserved null
    // record, so a null range decodes a position of zero and reads the first live allocation for
    // everything past it.
    dbg_assert(!ctx.submesh.vertexData.Null(), ErrorCode.kNullRawDeref, 0, 0, instance.submeshIndex);

    ctx.meshlet = forwardData.meshletBuffer.Get(ctx.submesh.meshlets, gid);

    // The material record is read only on a draw that culls here, and never for a submesh with no
    // material: the reserved null entry at offset zero must not be dereferenced, and an `||` would
    // still evaluate the operand. A draw that binds no material data at all -- the outline mask --
    // says it culls nothing, and so never reaches the read.
    ctx.cull = false;
    if (DrawCullsBackfaces() && !instance.material.Null())
    {
        ctx.cull = !IsMaterialDoubleSided(instance.material.GetByteOffset());
    }

    dbg_assert(
        ctx.meshlet.vertexCount <= cMaxVerticesPerMeshlet,
        ErrorCode.kMeshletVertexOverflow,
        ctx.meshlet.vertexCount,
        cMaxVerticesPerMeshlet,
        gid
    );
    dbg_assert(
        ctx.meshlet.triangleCount <= cMaxPrimsPerMeshlet,
        ErrorCode.kMeshletPrimitiveOverflow,
        ctx.meshlet.triangleCount,
        cMaxPrimsPerMeshlet,
        gid
    );

    return ctx;
}

public uint3 EvaluatePrimitive(MeshletContext ctx, uint t)
{
    return forwardData.indexBuffer.Get<3>(ctx.submesh.indices, ctx.meshlet.relativeIndexOffset + t * 3);
}

// The bind-pose vertex bytes, and the submesh-local index they were read at. A tier that fetches its
// position elsewhere still needs these: the UVs, the tangent frame and the skin weights are only ever
// in the vertex data.
public DecodedVertex DecodeMeshletVertex(MeshletContext ctx, SubmeshInstance instance, uint v, out uint vertexIdx)
{
    vertexIdx = forwardData.vertexMapBuffer[ctx.submesh.vertexMap, ctx.meshlet.relativeVertexOffset + v];

    dbg_assert(
        vertexIdx < ctx.submesh.vertexCount, ErrorCode.kInvalidVertexIndex, vertexIdx, ctx.submesh.vertexCount, instance.submeshIndex
    );

    uint vertexByteBase = ctx.submesh.vertexData.GetStart() + vertexIdx * ctx.submesh.layout.stride;

    return DecodeVertex(forwardData.vertexDataBuffer, ctx.submesh.layout, vertexByteBase);
}

// Both matrices carry their frame's sub-pixel offset; the raster keeps it and the motion vector must
// not, so it comes back out here rather than in the pixel stage -- undoing it in clip space costs one
// multiply-add and leaves ComputeMotionVector a plain difference.
public void ProjectVertex(inout ForwardVSOut vertexOut, float4 worldPos, float4 prevWorldPos)
{
    vertexOut.position = mul(viewData.viewProj, worldPos);

    vertexOut.clip = vertexOut.position;
    vertexOut.clip.xy -= viewData.jitter * vertexOut.position.w;

    vertexOut.prevClip = mul(viewData.prevViewProj, prevWorldPos);
    vertexOut.prevClip.xy -= viewData.prevJitter * vertexOut.prevClip.w;

    vertexOut.worldPos = worldPos.xyz;
}
