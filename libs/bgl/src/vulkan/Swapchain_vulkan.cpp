#include "swapchain/Swapchain.h"
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/device/Device.h>
#include <memory>

namespace bgl
{
	std::unique_ptr<Swapchain>
	CreateBackendSwapchain(
		const RenderTargetDesc&      desc,
		const bgpu::DeviceRef&       device,
		const bgpu::CommandQueueRef& queue,
		const bool                   enableDebug)
	{
		(void)desc;
		(void)device;
		(void)queue;
		(void)enableDebug;
		throw GraphicsError("A windowed render target is not on Vulkan yet; make it headless");
	}
}
