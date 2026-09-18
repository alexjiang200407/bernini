// Which GPU table a dispatch reads its start index into the compacted instance list out of. There
// are two because the paths order that list differently: opaque and alpha-test are grouped by draw
// bucket, transparent is sorted by depth.
public enum BaseTable : uint32_t
{
    kDrawBucketed = 0,
    kDepthSorted,
}
