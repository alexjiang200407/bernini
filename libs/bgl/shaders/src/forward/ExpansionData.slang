import types.ComputeBuffer;

// Which slice of the compacted instance list one dispatch covers. Read only by whatever expands
// instances into meshlet work -- the amplification stage on the mesh path -- so the vertex-pulling
// path, which a WebGPU vertex stage forbids these read-write buffers in, never binds it.
public struct ExpansionData
{
    public uint psoIndex;

    // Which GPU-side table holds this draw's base into the instance-index list: the opaque path is
    // bucketed by PSO, the transparent path by occlude partition. Neither base is a CPU value.
    public uint baseSource;
    public uint partitionIndex;

    public ComputeBuffer<uint> compactedInstances;
    public ComputeBuffer<uint> psoPrefixSum;
    public ComputeBuffer<uint> transparentPartitionBase;
};
