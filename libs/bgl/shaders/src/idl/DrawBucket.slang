// A draw bucket is the group of instances one indirect dispatch draws -- every instance with the
// same (geometry tier x material kind x layer). Its id is simultaneously the
// histogram slot, the prefix-sum lane, the indirect-arg element and every pass's kernel-table
// index. Ids are handed out densely by the CPU's DrawBucketTable; nothing here derives one.

// The capacity every count-sized structure is built to, so a grown bucket set changes no sizes:
// the cull chain's buffers and groupshared arrays, and the prefix scan's single thread group, at two
// lanes a bucket (below).
public static const uint32_t cMaxDrawBuckets = 256;

// A draw lane is what the cull counts, compacts and dispatches by: every bucket owns two, its own id
// for placements at rest and that id plus cDissolveLane for placements dissolving between levels of
// detail, which draw through the bucket's dissolve pipelines. The prefix scan runs over every lane,
// so cMaxDrawLanes is the ceiling util.cpp pins.
public static const uint32_t cDissolveLane = cMaxDrawBuckets;
public static const uint32_t cMaxDrawLanes = 2 * cMaxDrawBuckets;

// The draw bucket of an instance that resolved none; no pass counts or draws it.
public static const uint32_t cInvalidDrawBucket = uint32_t(-1);

// One bit of a draw bucket's flags word: what the GPU needs to know about a bucket that its id cannot
// say, one word per bucket up to cMaxDrawBuckets. A zeroed word is an opaque-shaped bucket.
public enum DrawBucketFlag : uint32_t
{
    // Drawn from the depth-sorted list rather than by its own indirect dispatch.
    kTransparent = 1,
}
;
