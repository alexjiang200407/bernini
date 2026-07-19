// THIS IS A FILE GENERATED FROM Constants.slang. DO NOT EDIT MANUALLY

// Engine-wide constants shared by the CPU and GPU. idlgen emits these as
// `constexpr` into bgl/src/idl/Constants.h and copies this module to
// bgl/shaders/src/idl/Constants.slang, so both sides originate from here.

// Meshlet capacity limits.
public static const uint cMaxPrimsPerMeshlet = 124;
public static const uint cMaxVerticesPerMeshlet = 64;

// Threads per mesh-shader group. A group cooperatively emits one meshlet, whose vertex and
// triangle counts both exceed this, so every emission loop must stride by it.
public static const uint cMeshGroupSize = 64;

// Threads per amplification-shader group: one group amplifies one submesh instance.
public static const uint cThreadsPerInstance = 64;

// Thread group sizes of the instance counting-sort compute shaders.
public static const uint cHistogramGroupSize = 256;
public static const uint cCompactGroupSize = 128;

// One workgroup sorts the whole transparent list in groupshared memory, which caps how many
// transparent instances a view can draw. A multi-group radix sort is the scale-up past this.
//
// Both are bounded by per-backend device limits, not just by taste. Groupshared use is
// cTransparentSortCapacity * 8 bytes = 8 KiB, under every target's floor (D3D12 32 KiB, Vulkan's
// guaranteed minimum 16 KiB). The group size is the tighter one: D3D12, Metal and desktop Vulkan
// all allow 1024 invocations, but Vulkan only *guarantees* 128, so a Vulkan backend must validate
// this against maxComputeWorkGroupInvocations rather than assume it.
public static const uint cTransparentSortCapacity = 1024; // must stay a power of two
public static const uint cTransparentSortGroupSize = 512; // one thread per compare-exchange pair

// Fills the sort's tail so padding never displaces a real entry. Reserved: TransparentDepthKeys
// caps real keys below it, or padding could sort into the drawn prefix.
public static const uint cSortPadKey = 0xFFFFFFFF;

// The sorted list is partitioned by occlude class: [self-occluding][plain]. Each partition is one
// indirect dispatch, so the draw needs no discovered run list.
public static const uint cTransparentPartitionCount = 2;
public static const uint cOccludePartition = 0;
public static const uint cPlainPartition = 1;

// Which GPU table the amplification shader takes its base into the instance-index list from.
public static const uint cBaseSourcePsoPrefixSum = 0;
public static const uint cBaseSourceTransparentPartition = 1;
