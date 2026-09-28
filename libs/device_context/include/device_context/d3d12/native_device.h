#pragma once
#include <device_context/api.h>

struct ID3D12Device;

namespace gpu
{
	class DeviceContext;

	/**
	 * The D3D12 device behind a context. Borrowed: the context holds the reference, and an owner
	 * that needs the device past the context's life retains it.
	 */
	DEVICE_CONTEXT_API ID3D12Device*
	GetD3d12Device(const DeviceContext& context) noexcept;
}
