import idl.Constants;
import idl.CullStats;
import idl.CullView;
import idl.Geom;
import idl.InstanceLod;
import idl.InstanceVisibility;
import idl.MeshInstance;
import idl.Submesh;
import lib.culling.occlusion;
import lib.culling.visibility_bits;
import lib.types.ComputeBuffer;
import lib.types.EntryBuffer;
import lib.types.PackedBuffer;
import lib.types.RangeBuffer;
import lib.data.SubmeshInstance;
import lib.types.UploadBuffer;

// Phase 2 of the occlusion cull, one thread per instance: a candidate CullInstances left
// (cVisibleCandidateBit in `candidates`, phase 1's words) is tested against the HZB built from
// phase 1's depth, through this draw's own view-projection, and draws if that does not hide it.
// Phase 2's words go to a buffer of their own, every slot written, so the compaction that follows
// under phase 2's scratch holds phase 2's draws alone and phase 1's words stay as they were for
// whatever reads them later. The level of detail is the word phase 1 chose; nothing is chosen again.

struct Uniforms
{
    UploadBuffer<CullView> cullView;
    PackedBuffer<SubmeshInstance> instanceBuffer;
    EntryBuffer<MeshInstance> meshBuffer;
    EntryBuffer<Geom> geomBuffer;
    RangeBuffer<Submesh> submeshBuffer;
    ComputeBuffer<InstanceVisibility> candidates;
    ComputeBuffer<InstanceVisibility> visibility;
    ComputeBuffer<InstanceLod> lodCurrent;
    ComputeBuffer<uint> drawnHistory;

    SamplerState.Handle hzbSampler;
    Texture2D.Handle hzb0;
    Texture2D.Handle hzb1;
    Texture2D.Handle hzb2;
    Texture2D.Handle hzb3;
    Texture2D.Handle hzb4;
    Texture2D.Handle hzb5;
    Texture2D.Handle hzb6;
    Texture2D.Handle hzb7;
    Texture2D.Handle hzb8;
    Texture2D.Handle hzb9;
    Texture2D.Handle hzb10;
    Texture2D.Handle hzb11;

    ComputeBuffer<CullStats> stats;
};

ConstantBuffer<Uniforms> gUniforms;

Texture2D.Handle HzbLevel(uint level)
{
    switch (level)
    {
    case 0u:
        return gUniforms.hzb0;
    case 1u:
        return gUniforms.hzb1;
    case 2u:
        return gUniforms.hzb2;
    case 3u:
        return gUniforms.hzb3;
    case 4u:
        return gUniforms.hzb4;
    case 5u:
        return gUniforms.hzb5;
    case 6u:
        return gUniforms.hzb6;
    case 7u:
        return gUniforms.hzb7;
    case 8u:
        return gUniforms.hzb8;
    case 9u:
        return gUniforms.hzb9;
    case 10u:
        return gUniforms.hzb10;
    default:
        return gUniforms.hzb11;
    }
}

[shader("compute")]
[numthreads(cHistogramGroupSize, 1, 1)]
void main(uint globalIdx: SV_DispatchThreadID)
{
    if ((gUniforms.candidates[globalIdx].visible & cVisibleCandidateBit) == 0u)
    {
        gUniforms.visibility[globalIdx].visible = 0u;
        return;
    }

    let instance = gUniforms.instanceBuffer[globalIdx];
    let mesh = gUniforms.meshBuffer.Get(instance.meshInstance);
    let geom = gUniforms.geomBuffer.Get(mesh.geom);
    let submesh = gUniforms.submeshBuffer.Get(geom.submeshes, geom.submeshes.EntryLod0(instance.submeshIndex));
    let sphere = mesh.TransformSphere(submesh.boundingSphere);
    let view = gUniforms.cullView[0];

    bool hidden = false;
    let query = PrepareHzbQuery(view, view.viewProj, sphere);
    if (!query.visible)
    {
        // The level differs from thread to thread, so the handle it picks is no uniform index.
        let gathered = nonuniform(HzbLevel(query.level)).GatherRed(gUniforms.hzbSampler, query.uv);
        hidden = HzbHides(gathered, query.nearest);
    }

    let placement = instance.meshInstance.GetOffset();
    let bits = hidden ? 0u : VisibilityBits(gUniforms.lodCurrent[placement], geom, true);
    gUniforms.visibility[globalIdx].visible = bits;
    if ((bits & (cVisibleCurrentBit | cVisibleOutgoingBit)) != 0u)
    {
        gUniforms.drawnHistory[globalIdx] |= cDrawnMeshBit;
    }

#if defined(BERNINI_GPU_DEBUG)
    if (hidden)
    {
        gUniforms.stats[0].occlusionCulled.add(1u);
    }
#endif
}
