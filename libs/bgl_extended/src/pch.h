#pragma once
#include <spdlog/sinks/basic_file_sink.h>

// Carries core's checks, which nearly every source here names.
#include <core/err/util.h>

#include <bgl/error.h>
#include <bgl/glm.h>

#include <bgpu/uniforms/DescriptorHandle.h>

#include <slang-com-ptr.h>
#include <slang.h>

#include <core/ref/Ref.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
