#pragma once

// Windows only, and empty elsewhere, as <bgpu/d3d12/D3d12ErrorChecker.h> is: a public header has to
// parse on every host the tools run on.
#if defined(_WIN32)

#	include <bgpu/api.h>
#	include <directx/d3d12.h>

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

#endif
