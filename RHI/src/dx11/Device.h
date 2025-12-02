#pragma once
#include <rhi/IDevice.h>
#include <rhi/ResourceHandle.h>
#include <rhi/detail/RefCounter.h>

namespace rhi::dx11
{
	struct Context
	{
		ResourceHandle<ID3D11Device> pDevice;
	};

	class Device : public detail::RefCounter<IDevice>
	{
	public:
		Device();
	};
}
