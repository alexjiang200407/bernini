#pragma once

// QtCore only, though the editor is mostly widgets. 56 of its 60 sources reach QtCore and putting it
// here measured 37% off the target's compile time; adding the QtGui and QtWidgets umbrellas on top
// measured no faster (71.0s against 68.1s, inside the noise) and grew the PCH from 52 MB to 64 MB.
//
// That size is the second axis a PCH is judged on. It is loaded by every compile, so it is paid
// per *parallel* job and again per checkout building at once -- twelve jobs across three checkouts
// is the difference between ~1.9 GB and ~2.3 GB resident in precompiled headers alone. Time and
// memory both count; see docs/build_performance.md.
#include <QtCore/QtCore>  // IWYU pragma: keep

// Never <glm/*> directly: core/glm.h sets GLM_FORCE_DEPTH_ZERO_TO_ONE and friends first, and glm
// reaching a TU unconfigured builds a projection matrix for the wrong depth range.
#include <core/glm.h>

#include <core/ref/Ref.h>        // IWYU pragma: keep
#include <core/ref/SharedRef.h>  // IWYU pragma: keep
