#include "dx11/Device.h"

namespace rhi::dx11
{
	Device::Device() {}
}

rhi::IDevice::Handle
rhi::IDevice::Create()
{
	return Handle(new dx11::Device());
}
