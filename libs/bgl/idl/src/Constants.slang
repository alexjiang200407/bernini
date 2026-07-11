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

public static const float PI = 3.14159265359f;
