// The single translation unit that emits metal-cpp's out-of-line symbols. Nothing else may define
// these macros, and this file is built with SKIP_PRECOMPILE_HEADERS: the PCH includes the umbrella,
// which would land before these defines and guard the definitions out.
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include "metal_cpp.h"
