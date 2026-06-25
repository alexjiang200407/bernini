#pragma once
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace bgl
{
	namespace logger = spdlog;
}

#include "error/gassert.h"

#include <bgl/glm.h>
#include <bgl/error.h>

#include <slang-com-ptr.h>
#include <slang.h>

#include <core/ref/Ref.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
