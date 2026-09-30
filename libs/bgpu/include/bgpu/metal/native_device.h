#pragma once
#include <bgpu/api.h>

// Declared, not defined: a caller that dereferences one includes <Metal/Metal.hpp> itself.
namespace MTL
{
	class Device;
}

namespace bgpu
{
	class GpuContext;

	/**
	 * The Metal device behind a context. Borrowed: the context holds the reference, and an owner
	 * that needs the device past the context's life retains it.
	 */
	BGPU_API MTL::Device*
			 GetMtlDevice(const GpuContext& context) noexcept;
}
