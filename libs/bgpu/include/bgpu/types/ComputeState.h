#pragma once

namespace bgpu
{
	struct ComputeKernel;

	struct ComputeState
	{
		const ComputeKernel* kernel = nullptr;
	};
}
