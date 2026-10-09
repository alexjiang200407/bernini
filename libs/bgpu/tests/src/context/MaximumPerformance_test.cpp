#include <bgpu/GpuContext.h>
#include <catch2/catch_test_macros.hpp>

#if defined(RENDERER_BACKEND_DX12)
#	include <combaseapi.h>
#	include <cstdint>
#	include <dxgi.h>
#	include <winerror.h>
#	include <wrl/client.h>

namespace
{
	constexpr uint32_t c_NvidiaVendorId = 0x10DE;

	// The adapter D3D12CreateDevice takes when it is given none, as the context's is.
	[[nodiscard]] uint32_t
	DefaultAdapterVendor()
	{
		Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
		auto                                  desc = DXGI_ADAPTER_DESC1();
		REQUIRE(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
		REQUIRE(SUCCEEDED(factory->EnumAdapters1(0, &adapter)));
		REQUIRE(SUCCEEDED(adapter->GetDesc1(&desc)));
		return desc.VendorId;
	}
}
#elif defined(RENDERER_BACKEND_VULKAN)
#	include "native_device_vulkan.h"
#	include "volk_vulkan.h"
#	include <algorithm>
#	include <cstdint>
#	include <string_view>
#	include <vector>

namespace
{
	[[nodiscard]] bool
	HasExtension(const bgpu::GpuContext& context, const std::string_view name)
	{
		const VkPhysicalDevice physical = bgpu::GetVulkanHandles(context).physicalDevice;
		uint32_t               count    = 0;
		REQUIRE(
			vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) == VK_SUCCESS);
		auto extensions = std::vector<VkExtensionProperties>(count);
		REQUIRE(
			vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()) ==
			VK_SUCCESS);
		return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
			return std::string_view(extension.extensionName) == name;
		});
	}
}
#endif

// The clock itself is only observable in a vsync'd window (docs/bgpu.md § Maximum performance);
// these pin what each backend tells the driver, and what it reports back.

TEST_CASE("Maximum performance is off unless a client asks for it", "[device][maxperf]")
{
	CHECK_FALSE(bgpu::GpuContextDesc().preferMaximumPerformance);

	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	CHECK(context->GetMaximumPerformance() == bgpu::MaximumPerformance::kNotRequested);
}

TEST_CASE("A request for maximum performance never fails the context", "[device][maxperf]")
{
	auto desc                     = bgpu::GpuContextDesc();
	desc.preferMaximumPerformance = true;

	auto context = bgpu::CreateGpuContext(desc);
	CHECK(context->GetMaximumPerformance() != bgpu::MaximumPerformance::kNotRequested);
#if defined(__APPLE__)
	CHECK(context->GetMaximumPerformance() == bgpu::MaximumPerformance::kUnavailable);
#elif defined(RENDERER_BACKEND_DX12)
	// Every driver since Reflex arrived (R455) takes the request; no other vendor's has it.
	const auto expected = DefaultAdapterVendor() == c_NvidiaVendorId ?
	                          bgpu::MaximumPerformance::kRequested :
	                          bgpu::MaximumPerformance::kUnavailable;
	CHECK(context->GetMaximumPerformance() == expected);
#elif defined(RENDERER_BACKEND_VULKAN)
	// Granted when the device is created with the extension; each swapchain then sets the boost.
	const bool boost = HasExtension(*context, VK_NV_LOW_LATENCY_2_EXTENSION_NAME) &&
	                   HasExtension(*context, VK_KHR_PRESENT_ID_EXTENSION_NAME);
	const auto expected =
		boost ? bgpu::MaximumPerformance::kRequested : bgpu::MaximumPerformance::kUnavailable;
	CHECK(context->GetMaximumPerformance() == expected);
#endif
}
