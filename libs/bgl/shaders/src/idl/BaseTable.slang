// THIS IS A FILE GENERATED FROM BaseTable.slang. DO NOT EDIT MANUALLY

// Which GPU table a dispatch reads its start index into the compacted instance list out of. There
// are two because the two material classes build that list differently: opaque and masked are
// bucketed by PSO, alpha-blended is depth-sorted and split into partitions.
public enum BaseTable : uint32_t
{
    kOpaqueMasked = 0,
    kAlphaBlend,
}
