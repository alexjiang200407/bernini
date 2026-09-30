// The grid a raw arena allocates on. 16 so a payload's float4 lands on its own natural alignment
// and a whole-struct load is never emitted wider than the address allows; the loads themselves only
// require 4.
public static const uint cRawBlockBytes = 16;

// Where a record's payload begins, measured from the header its RawEntry names. One block, so the
// payload keeps the grid's alignment rather than the header's four bytes.
public static const uint cRawPayloadOffset = 16;
