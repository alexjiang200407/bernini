// Which GPU table a dispatch reads its start index into the compacted instance list out of. There
// are two because the paths order that list differently: opaque and alpha-test are bucketed by PSO,
// transparent is depth-sorted and split into partitions.
public enum BaseTable : uint32_t
{
    kPsoBucketed = 0,
    kDepthSorted,
}
