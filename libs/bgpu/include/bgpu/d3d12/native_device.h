#pragma once
#include <bgpu/api.h>

struct ID3D12Device;

namespace bgpu
{
	class GpuContext;

	/**
	 * The D3D12 device behind a context. Borrowed: the context holds the reference, and an owner
	 * that needs the device past the context's life retains it.
	 */
	BGPU_API ID3D12Device*
	GetD3d12Device(const GpuContext& context) noexcept;
}
