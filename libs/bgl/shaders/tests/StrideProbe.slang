import idl.CullStats;
import idl.CullView;
import idl.DebugRecord;
import idl.DispatchArgs;
import idl.InstanceVisibility;
import idl.LoosePbrMaterial;
import idl.Mesh;
import idl.Meshlet;
import idl.MeshletInstance;
import idl.PbrMaterial;
import idl.Submesh;
import idl.VertexLayout;
import types.SubmeshInstance;

StructuredBuffer<Mesh> gMesh;
StructuredBuffer<Meshlet> gMeshlet;
StructuredBuffer<Submesh> gSubmesh;
StructuredBuffer<SubmeshInstance> gSubmeshInstance;
StructuredBuffer<MeshletInstance> gMeshletInstance;
StructuredBuffer<VertexLayout> gVertexLayout;
StructuredBuffer<CullView> gCullView;
StructuredBuffer<CullStats> gCullStats;
StructuredBuffer<InstanceVisibility> gInstanceVisibility;
StructuredBuffer<DispatchArgs> gDispatchArgs;
StructuredBuffer<PbrMaterial> gPbrMaterial;
StructuredBuffer<LoosePbrMaterial> gLoosePbrMaterial;
StructuredBuffer<DebugRecord> gDebugRecord;

RWStructuredBuffer<uint> gOut;

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    gOut[0] = 0u;
}
