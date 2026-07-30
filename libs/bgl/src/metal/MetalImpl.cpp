// The single translation unit that emits metal-cpp's out-of-line symbols. Nothing else may define
// these macros, and this file must not include the umbrella through the PCH (the PCH carries no
// metal-cpp headers) or the definitions would be guarded out.
#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include "metal_cpp.h"
