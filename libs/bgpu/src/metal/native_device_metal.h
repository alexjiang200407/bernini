#pragma once

// Declared, not defined: a caller that dereferences one includes <Metal/Metal.hpp> itself.
namespace MTL
{
	class Device;
}

namespace bgpu
{
	class GpuContext;

	/**
	 * The Metal device behind a context, for the RHI's own objects; an owner asks its IDevice
	 * (`GetNativeObject(kMtlDevice)`). Borrowed: the context holds the reference.
	 */
	MTL::Device*
	GetMtlDevice(const GpuContext& context) noexcept;
}
