// A swapchain's image driven through the RHI and presented beside it: the context enables what a
// window needs, a borrowed image takes the RHI's barriers into and out of kPresent, and a present
// waits for the RHI's submission through the queue's timeline. The renderer's Vulkan target does
// the same; this is it with nothing of the renderer around it.
//
// bgpu_tests globs every .cpp under tests/ whatever the backend, so a Vulkan-only case has to exclude
// itself: the headers below do not exist on any other build.
#if defined(RENDERER_BACKEND_VULKAN) && defined(_WIN32)
#	include "native_device_vulkan.h"
#	include "volk_vulkan.h"
#	include <Windows.h>
#	include <array>
#	include <bgpu/GpuContext.h>
#	include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#	include <bgpu/cmd/CommandList.h>
#	include <bgpu/cmd/CommandQueue.h>
#	include <bgpu/device/Device.h>
#	include <bgpu/resource/NativeTextureDesc.h>
#	include <bgpu/resource/Readback.h>
#	include <bgpu/resource/ResourceManager.h>
#	include <bgpu/resource/Texture.h>
#	include <bgpu/types/Barrier.h>
#	include <bgpu/types/Format.h>
#	include <bgpu/types/NativeObject.h>
#	include <bgpu/types/NativeVkQueue.h>
#	include <bgpu/types/QueueType.h>
#	include <catch2/catch_test_macros.hpp>
#	include <cstdint>
#	include <cstring>
#	include <limits>
#	include <mutex>
#	include <vector>

namespace
{
	constexpr uint32_t c_Width  = 4;
	constexpr uint32_t c_Height = 4;

	// Hidden, and a popup so its client area is its whole size: a surface needs a window, not one
	// anybody sees. "STATIC" is a class Windows registers.
	class HiddenWindow
	{
	public:
		HiddenWindow() :
			m_Hwnd(CreateWindowExW(
				0,
				L"STATIC",
				L"bgpu_tests present",
				WS_POPUP,
				0,
				0,
				static_cast<int>(c_Width),
				static_cast<int>(c_Height),
				nullptr,
				nullptr,
				GetModuleHandleW(nullptr),
				nullptr))
		{}

		~HiddenWindow() { DestroyWindow(m_Hwnd); }

		HiddenWindow(const HiddenWindow&) = delete;
		HiddenWindow(HiddenWindow&&)      = delete;
		HiddenWindow&
		operator=(const HiddenWindow&) = delete;
		HiddenWindow&
		operator=(HiddenWindow&&) = delete;

		[[nodiscard]] HWND
		Get() const noexcept
		{
			return m_Hwnd;
		}

	private:
		HWND m_Hwnd;
	};

	bgpu::TextureBarrierDesc
	Transition(
		const bgpu::BarrierSyncFlag   syncBefore,
		const bgpu::BarrierAccessFlag accessBefore,
		const bgpu::BarrierLayout     layoutBefore,
		const bgpu::BarrierSyncFlag   syncAfter,
		const bgpu::BarrierAccessFlag accessAfter,
		const bgpu::BarrierLayout     layoutAfter)
	{
		return bgpu::TextureBarrierDesc()
		    .AddSyncBefore(syncBefore)
		    .AddAccessBefore(accessBefore)
		    .SetLayoutBefore(layoutBefore)
		    .AddSyncAfter(syncAfter)
		    .AddAccessAfter(accessAfter)
		    .SetLayoutAfter(layoutAfter);
	}
}

TEST_CASE("A Vulkan device creates no texture in kPresent", "[vulkan]")
{
	auto contextDesc             = bgpu::GpuContextDesc();
	contextDesc.enableDebugLayer = true;
	auto device                  = bgpu::CreateDevice(bgpu::CreateGpuContext(contextDesc));
	auto rm                      = device->CreateResourceManager(bgpu::ResourceManagerDesc());

	auto desc          = bgpu::TextureDesc();
	desc.width         = c_Width;
	desc.height        = c_Height;
	desc.format        = bgpu::Format::BGRA8_UNORM;
	desc.initialLayout = bgpu::BarrierLayout::kPresent;
	desc.debugName     = "not a swapchain's";

	CHECK_FALSE(rm->ValidTextureHandle(rm->CreateTexture(desc)));
}

TEST_CASE(
	"A Vulkan swapchain's image is written through the RHI and presented behind its submission",
	"[vulkan][present]")
{
	auto contextDesc             = bgpu::GpuContextDesc();
	contextDesc.enableDebugLayer = true;
	contextDesc.strictError      = true;
	auto context                 = bgpu::CreateGpuContext(contextDesc);
	auto device                  = bgpu::CreateDevice(context);
	auto rm                      = device->CreateResourceManager(bgpu::ResourceManagerDesc());
	auto queue                   = device->CreateCommandQueue(bgpu::QueueType::kGraphics);
	rm->RegisterQueue(queue.Get());

	const auto* native =
		queue->GetNativeObject(bgpu::NativeObjectType::kVkQueue).As<bgpu::NativeVkQueue>();
	REQUIRE(native != nullptr);
	auto* const instance =
		device->GetNativeObject(bgpu::NativeObjectType::kVkInstance).As<VkInstance_T>();
	auto* const physical =
		device->GetNativeObject(bgpu::NativeObjectType::kVkPhysicalDevice).As<VkPhysicalDevice_T>();
	auto* const vkDevice =
		device->GetNativeObject(bgpu::NativeObjectType::kVkDevice).As<VkDevice_T>();

	const auto window = HiddenWindow();
	REQUIRE(window.Get() != nullptr);

	auto surfaceInfo      = VkWin32SurfaceCreateInfoKHR();
	surfaceInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceInfo.hinstance = GetModuleHandleW(nullptr);
	surfaceInfo.hwnd      = window.Get();
	VkSurfaceKHR surface  = VK_NULL_HANDLE;
	REQUIRE(vkCreateWin32SurfaceKHR(instance, &surfaceInfo, nullptr, &surface) == VK_SUCCESS);

	VkBool32 supported = VK_FALSE;
	REQUIRE(
		vkGetPhysicalDeviceSurfaceSupportKHR(physical, native->family, surface, &supported) ==
		VK_SUCCESS);
	REQUIRE(supported == VK_TRUE);

	auto capabilities = VkSurfaceCapabilitiesKHR();
	REQUIRE(
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities) == VK_SUCCESS);

	auto swapchainInfo             = VkSwapchainCreateInfoKHR();
	swapchainInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainInfo.surface          = surface;
	swapchainInfo.minImageCount    = std::max(2U, capabilities.minImageCount);
	swapchainInfo.imageFormat      = VK_FORMAT_B8G8R8A8_UNORM;
	swapchainInfo.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	swapchainInfo.imageExtent      = capabilities.currentExtent;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainInfo.preTransform     = capabilities.currentTransform;
	swapchainInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainInfo.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
	swapchainInfo.clipped          = VK_TRUE;
	VkSwapchainKHR swapchain       = VK_NULL_HANDLE;
	REQUIRE(vkCreateSwapchainKHR(vkDevice, &swapchainInfo, nullptr, &swapchain) == VK_SUCCESS);

	uint32_t count = 0;
	REQUIRE(vkGetSwapchainImagesKHR(vkDevice, swapchain, &count, nullptr) == VK_SUCCESS);
	auto images = std::vector<VkImage>(count);
	REQUIRE(vkGetSwapchainImagesKHR(vkDevice, swapchain, &count, images.data()) == VK_SUCCESS);

	auto fenceInfo   = VkFenceCreateInfo();
	fenceInfo.sType  = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence acquired = VK_NULL_HANDLE;
	REQUIRE(vkCreateFence(vkDevice, &fenceInfo, nullptr, &acquired) == VK_SUCCESS);

	uint32_t index = 0;
	REQUIRE(
		vkAcquireNextImageKHR(
			vkDevice,
			swapchain,
			std::numeric_limits<uint64_t>::max(),
			VK_NULL_HANDLE,
			acquired,
			&index) == VK_SUCCESS);
	REQUIRE(
		vkWaitForFences(vkDevice, 1, &acquired, VK_TRUE, std::numeric_limits<uint64_t>::max()) ==
		VK_SUCCESS);

	// Borrowed: no bgpu manager made it, so the import takes no reference and owes no transition.
	auto textureDesc                     = bgpu::TextureDesc();
	textureDesc.width                    = capabilities.currentExtent.width;
	textureDesc.height                   = capabilities.currentExtent.height;
	textureDesc.format                   = bgpu::Format::BGRA8_UNORM;
	textureDesc.initialLayout            = bgpu::BarrierLayout::kPresent;
	textureDesc.debugName                = "swapchain image";
	const bgpu::TextureHandle backbuffer = rm->ImportNativeTexture(
		bgpu::NativeTextureDesc()
			.SetObject(bgpu::NativeObjectType::kVkImage, bgpu::NativeObject{ images[index] })
			.SetTexture(textureDesc));
	REQUIRE(rm->ValidTextureHandle(backbuffer));

	auto texels = std::vector<uint8_t>(
		static_cast<size_t>(textureDesc.width) * textureDesc.height * 4,
		uint8_t{ 0x5a });
	const auto source =
		bgpu::TextureSubresourceData{ texels.data(), textureDesc.width * 4ULL, texels.size() };
	const auto layout       = rm->GetTextureReadbackLayout(backbuffer);
	auto       readbackDesc = bgpu::ReadbackBufferDesc();
	readbackDesc.byteSize   = layout.totalBytes;
	readbackDesc.debugName  = "swapchain image readback";
	const auto readback     = rm->CreateReadbackBuffer(readbackDesc);
	REQUIRE_FALSE(readback.IsNull());

	auto alloc    = device->CreateCommandAllocator();
	auto listDesc = bgpu::CommandListDesc();
	listDesc.type = bgpu::QueueType::kGraphics;
	auto list     = device->CreateCommandList(listDesc, alloc, rm);

	using Sync   = bgpu::BarrierSyncFlag;
	using Access = bgpu::BarrierAccessFlag;
	using Layout = bgpu::BarrierLayout;
	list->Open(queue.Get(), alloc.Get());
	// A freshly acquired image's contents are undefined, whatever it was presented in before.
	list->Barrier(
		backbuffer,
		Transition(
			Sync::kNone,
			Access::kNone,
			Layout::kUndefined,
			Sync::kCopy,
			Access::kCopyDest,
			Layout::kCopyDest));
	list->WriteTexture(backbuffer, { &source, 1 });
	list->Barrier(
		backbuffer,
		Transition(
			Sync::kCopy,
			Access::kCopyDest,
			Layout::kCopyDest,
			Sync::kCopy,
			Access::kCopySource,
			Layout::kCopySource));
	list->CopyTextureToReadback(readback, backbuffer);
	list->Barrier(
		backbuffer,
		Transition(
			Sync::kCopy,
			Access::kCopySource,
			Layout::kCopySource,
			Sync::kNone,
			Access::kNone,
			Layout::kPresent));
	list->Close();
	const uint64_t frame = queue->ExecuteCommandList(list.Get());

	auto semaphoreInfo   = VkSemaphoreCreateInfo();
	semaphoreInfo.sType  = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	VkSemaphore rendered = VK_NULL_HANDLE;
	REQUIRE(vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &rendered) == VK_SUCCESS);

	auto wait      = VkSemaphoreSubmitInfo();
	wait.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	wait.semaphore = static_cast<VkSemaphore>(native->timeline);
	wait.value     = frame;
	wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	auto signal      = VkSemaphoreSubmitInfo();
	signal.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signal.semaphore = rendered;
	signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	auto submit                     = VkSubmitInfo2();
	submit.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.waitSemaphoreInfoCount   = 1;
	submit.pWaitSemaphoreInfos      = &wait;
	submit.signalSemaphoreInfoCount = 1;
	submit.pSignalSemaphoreInfos    = &signal;

	auto present               = VkPresentInfoKHR();
	present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores    = &rendered;
	present.swapchainCount     = 1;
	present.pSwapchains        = &swapchain;
	present.pImageIndices      = &index;
	{
		auto* const vkQueue = static_cast<VkQueue>(native->queue);
		const auto  lock    = std::scoped_lock(*native->submitLock);
		REQUIRE(vkQueueSubmit2(vkQueue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS);
		const VkResult presented = vkQueuePresentKHR(vkQueue, &present);
		CHECK((presented == VK_SUCCESS || presented == VK_SUBOPTIMAL_KHR));
	}

	queue->Flush();
	const auto* read = static_cast<const uint8_t*>(rm->MapReadback(readback));
	REQUIRE(read != nullptr);
	CHECK(std::memcmp(read + layout.offset, texels.data(), textureDesc.width * 4) == 0);
	rm->UnmapReadback(readback);

	// The present's semaphore wait is not a fence: idle the queue before destroying what it holds.
	{
		const auto lock = std::scoped_lock(*native->submitLock);
		REQUIRE(vkQueueWaitIdle(static_cast<VkQueue>(native->queue)) == VK_SUCCESS);
	}
	rm->DestroyReadbackBuffer(readback, false);
	rm->DestroyTexture(backbuffer, false);
	rm->UnregisterQueue(queue.Get());
	vkDestroySemaphore(vkDevice, rendered, nullptr);
	vkDestroyFence(vkDevice, acquired, nullptr);
	vkDestroySwapchainKHR(vkDevice, swapchain, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
}
#endif
