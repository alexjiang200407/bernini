#pragma once
#include <bgpu/d3d12/D3d12ErrorChecker.h>

namespace bgl
{
	// `hr >> d3d12ErrChecker` reads the same in the renderer's backend as in bgpu, where the checker
	// lives because the device is created there.
	using bgpu::d3d12ErrChecker;
}
