#pragma once

#define GLM_FORCE_INTRINSICS
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

// Exported, not merely kept: reaching glm through this header is the rule, because glm parsed
// without the defines above builds a projection matrix for the wrong depth range. The pragma is
// what makes the tools agree -- <core/glm.h> counts as the include that provides glm::vec3, so a
// missing-include fix offers this and never <glm/ext.hpp>.
#include <glm/ext.hpp>  // IWYU pragma: export
#include <glm/glm.hpp>  // IWYU pragma: export
