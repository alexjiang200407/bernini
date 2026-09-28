#pragma once

// metal-cpp umbrella, in declaration mode: bgpu's MetalImpl.cpp emits the out-of-line
// symbols once for the whole process, so nothing here may define the *_PRIVATE_IMPLEMENTATION macros.
#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
