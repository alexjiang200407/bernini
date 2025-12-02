#pragma once
#include <rhi/IResource.h>
#include <rhi/ResourceHandle.h>

namespace rhi
{
	class IDevice : public IResource
	{
	public:
		~IDevice() = default;
	};

	using DeviceHandle = ResourceHandle<IDevice>;
}
