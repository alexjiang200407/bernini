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

// Threads per amplification-shader group. One group amplifies one submesh instance, and DispatchMesh
// takes group-uniform arguments and runs once per group, so a second lane has nothing of its own to
// do -- it would re-read the same instance to agree on the same dispatch.
public static const uint cThreadsPerInstance = 1;

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

// A root bone's `parent`, matching assetlib's own sentinel for one.
public static const uint cInvalidBone = 0xFFFFFFFF;

// Bones that may influence one vertex. joints0/weights0 are a vec4 pair, so the importer that writes
// them and the shader that blends them both work in fours; assetlib names the same number.
public static const uint cInfluencesPerVertex = 4;

// float4s one bone's palette entry occupies. A skinning matrix's last row is always (0,0,0,1), so it
// is stored as three rows rather than four -- the palette is the largest per-instance allocation in
// the frame, and motion vectors double it.
public static const uint cFloat4sPerBone = 3;

// Threads per pose-pass workgroup. One group poses one instance, one thread a bone at a time, so a
// rig with more bones than this strides.
public static const uint cPoseGroupSize = 64;
