#include "gfx/frame_constants.h"
#include "swapchain/Swapchain.h"
#include "volk_vulkan.h"
#include <Windows.h>
#include <algorithm>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/device/Device.h>
#include <bgpu/resource/NativeTextureDesc.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/NativeObject.h>
#include <bgpu/types/NativeVkQueue.h>
#include <bgpu/types/TextureDimension.h>
#include <core/err/util.h>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>
#include <vector>
#include <vulkan/vk_enum_string_helper.h>

namespace bgl
{
	namespace
	{
		// Vulkan's swapchains may be sRGB, where DXGI's flip model may not: the image is the format
		// the views write, and no mutable-format swapchain is needed.
		constexpr VkFormat     c_Format        = VK_FORMAT_B8G8R8A8_SRGB;
		constexpr bgpu::Format c_TextureFormat = bgpu::Format::SBGRA8_UNORM;

		// What the renderer does to a backbuffer: draws into it, and clears it with a transfer.
		constexpr VkImageUsageFlags c_Usage =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		constexpr uint64_t c_NoTimeout = std::numeric_limits<uint64_t>::max();

		void
		Check(const VkResult result, const char* call)
		{
			if (result != VK_SUCCESS)
			{
				throw GraphicsError(std::format("{} failed: {}", call, string_VkResult(result)));
			}
		}

		class VulkanSwapchain final : public Swapchain
		{
		public:
			VulkanSwapchain(
				const RenderTargetDesc&      desc,
				const bgpu::DeviceRef&       device,
				const bgpu::CommandQueueRef& queue) :
				m_Queue(queue), m_Native(*queue->GetNativeObject(bgpu::NativeObjectType::kVkQueue)
			                                  .As<bgpu::NativeVkQueue>()),
				m_Instance(device->GetNativeObject(bgpu::NativeObjectType::kVkInstance)
			                   .As<VkInstance_T>()),
				m_Physical(device->GetNativeObject(bgpu::NativeObjectType::kVkPhysicalDevice)
			                   .As<VkPhysicalDevice_T>()),
				m_Device(
					device->GetNativeObject(bgpu::NativeObjectType::kVkDevice).As<VkDevice_T>())
			{
				if (desc.wnd == nullptr)
				{
					throw GraphicsError(
						"Vulkan backend: a windowed render target needs an HWND in "
						"RenderTargetDesc::wnd");
				}

				auto surfaceInfo      = VkWin32SurfaceCreateInfoKHR();
				surfaceInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
				surfaceInfo.hinstance = GetModuleHandleW(nullptr);
				surfaceInfo.hwnd      = static_cast<HWND>(desc.wnd);
				Check(
					vkCreateWin32SurfaceKHR(m_Instance, &surfaceInfo, nullptr, &m_Surface),
					"vkCreateWin32SurfaceKHR");

				VkBool32 supported = VK_FALSE;
				Check(
					vkGetPhysicalDeviceSurfaceSupportKHR(
						m_Physical,
						m_Native.family,
						m_Surface,
						&supported),
					"vkGetPhysicalDeviceSurfaceSupportKHR");
				if (supported != VK_TRUE)
				{
					vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
					throw GraphicsError(
						"Vulkan backend: the renderer's queue cannot present to this window");
				}

				auto fenceInfo  = VkFenceCreateInfo();
				fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				Check(vkCreateFence(m_Device, &fenceInfo, nullptr, &m_Acquired), "vkCreateFence");

				Create(static_cast<uint32_t>(desc.width), static_cast<uint32_t>(desc.height));
				core::ensure(Acquire(), "a swapchain made for the window is out of date at once");
			}

			~VulkanSwapchain() noexcept override
			{
				IdleQueue();
				DestroySwapchain();
				vkDestroyFence(m_Device, m_Acquired, nullptr);
				vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
			}

			VulkanSwapchain(const VulkanSwapchain&) = delete;
			VulkanSwapchain(VulkanSwapchain&&)      = delete;
			VulkanSwapchain&
			operator=(const VulkanSwapchain&) = delete;
			VulkanSwapchain&
			operator=(VulkanSwapchain&&) = delete;

			[[nodiscard]] std::vector<bgpu::NativeTextureDesc>
			GetImages() const override
			{
				auto textureDesc          = bgpu::TextureDesc();
				textureDesc.format        = c_TextureFormat;
				textureDesc.width         = m_Extent.width;
				textureDesc.height        = m_Extent.height;
				textureDesc.dimension     = bgpu::TextureDimension::kTexture2D;
				textureDesc.usage         = bgpu::TextureUsageFlag::kRenderTarget;
				textureDesc.initialLayout = bgpu::BarrierLayout::kPresent;

				auto images = std::vector<bgpu::NativeTextureDesc>();
				for (VkImage image : m_Images)
				{
					images.push_back(
						bgpu::NativeTextureDesc()
							.SetObject(
								bgpu::NativeObjectType::kVkImage,
								bgpu::NativeObject{ image })
							.SetTexture(textureDesc));
				}
				return images;
			}

			[[nodiscard]] bgpu::Format
			GetViewFormat() const noexcept override
			{
				return c_TextureFormat;
			}

			[[nodiscard]] uint32_t
			GetCurrentImage() const noexcept override
			{
				return m_Current;
			}

			[[nodiscard]] bool
			StartsUndefined() const noexcept override
			{
				return true;
			}

			// An image belongs to the presentation engine from its present until it is acquired again.
			[[nodiscard]] bool
			CanReadPresented() const noexcept override
			{
				return false;
			}

			[[nodiscard]] bool
			Present(const uint64_t frameFence) noexcept override
			{
				// The present can wait only on a binary semaphore, so an empty batch turns the
				// queue's timeline value for the frame into one.
				auto wait      = VkSemaphoreSubmitInfo();
				wait.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
				wait.semaphore = static_cast<VkSemaphore>(m_Native.timeline);
				wait.value     = frameFence;
				wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

				auto signal      = VkSemaphoreSubmitInfo();
				signal.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
				signal.semaphore = m_Rendered[m_Current];
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
				present.pWaitSemaphores    = &m_Rendered[m_Current];
				present.swapchainCount     = 1;
				present.pSwapchains        = &m_Swapchain;
				present.pImageIndices      = &m_Current;

				VkResult presented = VK_SUCCESS;
				{
					const auto lock = std::scoped_lock(*m_Native.submitLock);
					core::ensure(
						vkQueueSubmit2(Queue(), 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS,
						"the present's submission failed");
					presented = vkQueuePresentKHR(Queue(), &present);
				}

				if (presented == VK_SUCCESS || presented == VK_SUBOPTIMAL_KHR)
				{
					if (Acquire())
						return false;
				}
				else
				{
					core::ensure(
						presented == VK_ERROR_OUT_OF_DATE_KHR,
						"vkQueuePresentKHR failed: {}",
						string_VkResult(presented));
				}

				// The window no longer matches its images: remade at the window's size, which the
				// target then takes on.
				const VkExtent2D window = WindowExtent();
				core::ensure(
					window.width != 0 && window.height != 0,
					"Vulkan swapchain: a minimized window has no size to present at, and a "
					"windowed "
					"target on Vulkan does not wait for one yet");
				Remake(window.width, window.height);
				return true;
			}

			void
			Resize(const uint32_t width, const uint32_t height) override
			{
				Remake(width, height);
			}

		private:
			[[nodiscard]] VkQueue
			Queue() const noexcept
			{
				return static_cast<VkQueue>(m_Native.queue);
			}

			[[nodiscard]] VkExtent2D
			WindowExtent() const noexcept
			{
				auto caps = VkSurfaceCapabilitiesKHR();
				core::ensure(
					vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Physical, m_Surface, &caps) ==
						VK_SUCCESS,
					"vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed");
				return caps.currentExtent;
			}

			void
			IdleQueue() const noexcept
			{
				m_Queue->Flush();
				// The present's own batches are outside the RHI's fence.
				const auto lock = std::scoped_lock(*m_Native.submitLock);
				(void)vkQueueWaitIdle(Queue());
			}

			void
			Remake(const uint32_t width, const uint32_t height)
			{
				IdleQueue();
				DestroySwapchain();
				Create(width, height);
				core::ensure(Acquire(), "a swapchain made for the window is out of date at once");
			}

			void
			Create(const uint32_t width, const uint32_t height)
			{
				auto caps = VkSurfaceCapabilitiesKHR();
				Check(
					vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Physical, m_Surface, &caps),
					"vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

				m_Extent.width =
					std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
				m_Extent.height =
					std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
				if (m_Extent.width != width || m_Extent.height != height)
				{
					spdlog::warn(
						"Vulkan swapchain: the window takes {}x{}, not the target's {}x{}",
						m_Extent.width,
						m_Extent.height,
						width,
						height);
				}

				auto imageCount = std::max(caps.minImageCount, c_SwapchainImageCount);
				if (caps.maxImageCount != 0)
					imageCount = std::min(imageCount, caps.maxImageCount);

				auto info             = VkSwapchainCreateInfoKHR();
				info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
				info.surface          = m_Surface;
				info.minImageCount    = imageCount;
				info.imageFormat      = c_Format;
				info.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
				info.imageExtent      = m_Extent;
				info.imageArrayLayers = 1;
				info.imageUsage       = c_Usage;
				info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
				info.preTransform     = caps.currentTransform;
				info.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
				// DXGI's Present(1, 0): one image per vertical blank.
				info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
				info.clipped     = VK_TRUE;
				Check(
					vkCreateSwapchainKHR(m_Device, &info, nullptr, &m_Swapchain),
					"vkCreateSwapchainKHR");

				uint32_t count = 0;
				Check(
					vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &count, nullptr),
					"vkGetSwapchainImagesKHR");
				m_Images.resize(count);
				Check(
					vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &count, m_Images.data()),
					"vkGetSwapchainImagesKHR");

				auto semaphoreInfo  = VkSemaphoreCreateInfo();
				semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
				m_Rendered.assign(count, VK_NULL_HANDLE);
				for (VkSemaphore& rendered : m_Rendered)
					Check(
						vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &rendered),
						"vkCreateSemaphore");
			}

			void
			DestroySwapchain() noexcept
			{
				for (VkSemaphore rendered : m_Rendered)
					vkDestroySemaphore(m_Device, rendered, nullptr);
				m_Rendered.clear();
				m_Images.clear();
				vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
				m_Swapchain = VK_NULL_HANDLE;
			}

			// Waits for the next image, as DXGI's Present waits for a free buffer. False when the
			// swapchain no longer matches the window and nothing was acquired.
			[[nodiscard]] bool
			Acquire() noexcept
			{
				const VkResult acquired = vkAcquireNextImageKHR(
					m_Device,
					m_Swapchain,
					c_NoTimeout,
					VK_NULL_HANDLE,
					m_Acquired,
					&m_Current);
				if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
					return false;
				core::ensure(
					acquired == VK_SUCCESS || acquired == VK_SUBOPTIMAL_KHR,
					"vkAcquireNextImageKHR failed: {}",
					string_VkResult(acquired));

				core::ensure(
					vkWaitForFences(m_Device, 1, &m_Acquired, VK_TRUE, c_NoTimeout) == VK_SUCCESS,
					"waiting for a swapchain image failed");
				core::ensure(
					vkResetFences(m_Device, 1, &m_Acquired) == VK_SUCCESS,
					"vkResetFences failed");
				return true;
			}

			bgpu::CommandQueueRef m_Queue;
			bgpu::NativeVkQueue   m_Native;
			VkInstance            m_Instance = VK_NULL_HANDLE;
			VkPhysicalDevice      m_Physical = VK_NULL_HANDLE;
			VkDevice              m_Device   = VK_NULL_HANDLE;

			VkSurfaceKHR   m_Surface   = VK_NULL_HANDLE;
			VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
			VkExtent2D     m_Extent{};
			VkFence        m_Acquired = VK_NULL_HANDLE;
			uint32_t       m_Current  = 0;

			std::vector<VkImage> m_Images;
			// One per image: the semaphore its present waits on is free again once it is reacquired.
			std::vector<VkSemaphore> m_Rendered;
		};
	}

	std::unique_ptr<Swapchain>
	CreateBackendSwapchain(
		const RenderTargetDesc&      desc,
		const bgpu::DeviceRef&       device,
		const bgpu::CommandQueueRef& queue,
		const bool                   enableDebug)
	{
		(void)enableDebug;
		return std::make_unique<VulkanSwapchain>(desc, device, queue);
	}
}
