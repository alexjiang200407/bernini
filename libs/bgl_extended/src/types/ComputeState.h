#pragma once

namespace bgl
{
	struct ComputeKernel;

	struct ComputeState
	{
		const ComputeKernel* kernel = nullptr;
	};
}
