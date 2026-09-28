#pragma once
#include <bgpu/GpuContext.h>
#include <cstdint>
#include <string>
#include <string_view>

namespace crowd
{
	/** Where the kernel reads one parameter: a register on D3D12, a [[buffer(N)]] index on Metal. */
	struct KernelBinding
	{
		uint32_t index = 0;
		uint32_t space = 0;
	};

	/** One compute entry point as the context's device consumes it. */
	struct KernelCode
	{
		// DXIL on D3D12, MSL text on Metal: whatever the context's sessions target.
		std::string code;

		KernelBinding params;
		KernelBinding output;
		uint32_t      threadsPerGroup = 1;
	};

	/**
	 * Compiles `main` of `moduleName` through the context's sessions on the calling thread, and
	 * reflects where `gParams` and `gOutput` are bound. Holds no slang:: object on return.
	 */
	[[nodiscard]] KernelCode
	CompileKernel(bgpu::GpuContext& context, std::string_view moduleName);

	inline constexpr std::string_view c_HashFillModule = "crowd.CSHashFill";
}
