import types.ComputeBuffer;

// Which slice of the compacted instance list one dispatch covers.
public struct ExpansionData
{
    public uint psoIndex;

    // Where in `compactedInstances` this dispatch's instances start is a GPU value, so what the CPU
    // supplies is which table to read it out of -- the opaque path is bucketed by PSO, the
    // transparent path by occlude partition.
    public uint baseTable;

    // Which half of the depth-sorted transparent list: [self-occluding][plain].
    public uint partitionIndex;

    public ComputeBuffer<uint> compactedInstances;
    public ComputeBuffer<uint> psoPrefixSum;
    public ComputeBuffer<uint> transparentPartitionBase;
};

public ConstantBuffer<ExpansionData> expansionData;
