#include <rhi/dx11/Device.h>

namespace rhi::dx11
{
	DeviceHandle
	Device::Create()
	{
		return DeviceHandle(new Device());
	}

	Device::Device() {}
}
