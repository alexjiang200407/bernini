#pragma once
#include <spdlog/spdlog.h>  // IWYU pragma: keep

namespace game
{
	// The alias every source here logs through. It lives in a header rather than in `pch.h` because
	// a precompiled header is a build optimisation and not an interface: the MSVC editor-SDK build
	// compiles this library without one.
	namespace logger = spdlog;
}
