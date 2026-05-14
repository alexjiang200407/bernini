#pragma once
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace bgl
{
	namespace logger = spdlog;
}

#include "error/gassert.h"

#define GLM_FORCE_INTRINSICS
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include <glm/ext.hpp>
#include <glm/glm.hpp>

#include <slang-com-ptr.h>
#include <slang.h>
