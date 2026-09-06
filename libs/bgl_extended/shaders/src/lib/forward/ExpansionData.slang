import idl.BaseTable;
import lib.types.ComputeBuffer;

// Which slice of the compacted instance list one dispatch covers.
public struct ExpansionData
{
    public uint psoIndex;

    // Where in `compactedInstances` this dispatch's instances start is a GPU value, so what the CPU
    // supplies is which table to read it out of.
    public BaseTable baseTable;

    public ComputeBuffer<uint> compactedInstances;
    public ComputeBuffer<uint> psoPrefixSum;

    // Non-zero on a draw whose pipeline draws both sides, so a single-sided material's back faces
    // are the mesh stage's to cull; zero where the pipeline culls in hardware, and on a draw that
    // wants the whole silhouette whichever way its triangles face.
    public uint cullBackfaces;
};

public ConstantBuffer<ExpansionData> expansionData;

// The opaque base is the exclusive prefix sum of the prior PSOs' counts; the depth-sorted list is
// drawn whole and starts at zero. Shared so the two stages that expand instances cannot disagree.
public uint ResolveInstanceBase()
{
    if (expansionData.baseTable == BaseTable::kDepthSorted)
    {
        return 0u;
    }

    return (expansionData.psoIndex == 0u) ? 0u : expansionData.psoPrefixSum[expansionData.psoIndex - 1u];
}
