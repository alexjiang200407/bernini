#pragma once
#include <rhi/IDevice.h>
#include <rhi/RefCounter.h>

namespace rhi::dx11
{
	class Device : public detail::RefCounter<IDevice>
	{
	public:
		[[nodiscard]]
		static DeviceHandle
		Create();

	private:
		Device();
	};
}
