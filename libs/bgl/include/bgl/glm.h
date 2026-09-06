#pragma once
// The glm configuration now lives in core so bgl and assetlib can share it without
// depending on each other. Kept here for source/backwards compatibility.
// Exported: this header is nothing but the re-export, so a file reaching glm through
// it is using it. See libs/core/include/core/glm.h.
#include <core/glm.h>  // IWYU pragma: export
