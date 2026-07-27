// One meshlet of one visible instance: what the amplification shader passes as a payload on D3D12,
// and what the WebGPU expansion pass writes to memory instead, since a vertex-pulling draw has no
// payload to receive.
public struct MeshletInstance
{
    // Already resolved through the compacted list, so the draw indexes the instance buffer directly
    // and never binds the compaction output -- which is read-write, and so cannot reach the vertex
    // stage under WebGPU.
    public uint instanceIndex;
    public uint meshletIndex;
}
