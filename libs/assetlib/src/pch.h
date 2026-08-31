#pragma once

// Never <glm/*> directly: core/glm.h sets GLM_FORCE_DEPTH_ZERO_TO_ONE and friends first, and glm
// reaching a TU unconfigured builds a projection matrix for the wrong depth range. 40 of assetlib's
// 59 sources reach glm, and putting it here measured 16% off the target's compile time.
#include <core/glm.h>

// Three headers are deliberately absent, each for its own reason.
//
// nlohmann/json.hpp because it *cost* 28% when it was here. A PCH is deserialized into every
// translation unit of a target, not only the ones that need it, so a ~900 KB header reached by 7 of
// 59 sources is paid for by the other 52. Measure before adding one: the win is the header's size
// times the fraction of sources that include it, and the loss is its size times all of them.
//
// tiny_gltf.h and stb_image.h because bmesh_gltf.cpp defines their *_IMPLEMENTATION macros before
// including them, and both put the implementation outside the include guard -- a PCH would get
// there first and guard the definitions out.
