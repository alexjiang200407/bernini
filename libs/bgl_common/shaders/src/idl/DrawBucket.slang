// A draw bucket is the group of instances one indirect dispatch draws -- every instance with the
// same (geometry tier x material kind x layer). Its id is simultaneously the
// histogram slot, the prefix-sum lane, the indirect-arg element and every pass's kernel-table
// index. Ids are handed out densely by the CPU's DrawBucketTable; nothing here derives one.

// The capacity every count-sized structure is built to, so a grown bucket set changes no sizes:
// the cull chain's buffers and groupshared arrays, and the prefix scan's single thread group --
// which is the hard ceiling (util.cpp pins this under a thread group's 1024).
public static const uint32_t cMaxDrawBuckets = 256;

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
