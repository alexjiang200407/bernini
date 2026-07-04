// THIS IS A FILE GENERATED FROM Constants.slang. DO NOT EDIT MANUALLY

// Engine-wide constants shared by the CPU and GPU. idlgen emits these as
// `constexpr` into bgl/src/idl/Constants.h and copies this module to
// bgl/shaders/src/idl/Constants.slang, so both sides originate from here.

// Meshlet capacity limits.
public static const uint cMaxPrimsPerMeshlet = 124;
public static const uint cMaxVerticesPerMeshlet = 64;

// Meshlet / instance acceleration-structure grouping.
public static const uint cMaxMeshletsPerAccelerationStructure = 64;
public static const uint cInstancesPerAccelerationStructureGroup = 32;
public static const uint cThreadsPerInstance = 64;

// Thread group sizes of the instance counting-sort compute shaders.
public static const uint cHistogramGroupSize = 256;
public static const uint cCompactGroupSize = 128;
