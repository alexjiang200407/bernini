#pragma once
#include <device_context/api.h>

namespace MTL  // NOLINT(readability-identifier-naming): metal-cpp's own namespace
{
	class Device;
}

namespace gpu
{
	class DeviceContext;

	/**
	 * The Metal device behind a context. Borrowed: the context holds the reference, and an owner
	 * that needs the device past the context's life retains it.
	 */
	DEVICE_CONTEXT_API MTL::Device*
					   GetMtlDevice(const DeviceContext& context) noexcept;
}
