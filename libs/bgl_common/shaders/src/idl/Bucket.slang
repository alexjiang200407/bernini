// A bucket is the group of instances one indirect dispatch draws: its id is simultaneously the
// histogram slot, the prefix-sum lane, the indirect-arg element and every pass's kernel-table
// index. Ids are handed out densely by the CPU's BucketTable; nothing here derives one.

// The capacity every count-sized structure is built to, so a grown bucket set changes no sizes:
// the cull chain's buffers and groupshared arrays, and the prefix scan's single thread group --
// which is the hard ceiling (util.cpp pins this under a thread group's 1024).
public static const uint32_t cMaxBuckets = 256;

// The bucket of an instance that resolved none; no pass counts or draws it.
public static const uint32_t cInvalidBucket = uint32_t(-1);
