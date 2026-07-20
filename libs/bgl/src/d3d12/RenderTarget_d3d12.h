#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandQueue.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "gfx/RenderTargetBase.h"
#include "resource/Dsv.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv.h"
#include <core/ref/RefCounter.h>

namespace bgl
{
	struct TextureRtvHandle
	{
		TextureHandle textureHandle;
		RtvHandle     rtvHandle;
	};

	struct TextureDsvHandle
	{
		TextureHandle textureHandle;
		DsvHandle     dsvHandle;
	};

	/**
	 * A per-output swapchain (windowed) or offscreen backbuffer ring (headless) plus a
	 * depth buffer, owned independently of Graphics so one renderer can drive many
	 * outputs. The frame ring is reached through RenderTargetBase, so frame-driving code
	 * needs neither this type nor D3D12.
	 */
	class RenderTarget : public core::RefCounter<RenderTargetBase>
	{
	public:
		RenderTarget(
			const RenderTargetDesc& desc,
			DeviceRef               device,
			CommandQueueRef         queue,
			ResourceManagerRef      resourceManager,
			bool                    enableDebug);

		~RenderTarget() noexcept override;

		RenderTarget(const RenderTarget&) noexcept = delete;
		RenderTarget(RenderTarget&&) noexcept      = delete;

		RenderTarget&
		operator=(const RenderTarget&) noexcept = delete;

		RenderTarget&
		operator=(RenderTarget&&) noexcept = delete;

		[[nodiscard]] uint32_t
		GetWidth() const noexcept override
		{
			return static_cast<uint32_t>(m_Width);
		}

		[[nodiscard]] uint32_t
		GetHeight() const noexcept override
		{
			return static_cast<uint32_t>(m_Height);
		}

		[[nodiscard]] uint32_t
		FrameSlot() const noexcept override
		{
			return m_FrameIndex;
		}

		[[nodiscard]] uint32_t
		LastPresentedSlot() const noexcept override
		{
			return m_LastPresentedIndex;
		}

		[[nodiscard]] bool
		IsHeadless() const noexcept override
		{
			return m_Headless;
		}

		[[nodiscard]] uint64_t
		SlotFence(uint32_t slot) const noexcept override
		{
			gassert(slot < c_SwapchainImageCount, "Frame slot out of range");
			return m_FenceValues[slot];
		}

		void
		SetSlotFence(uint32_t slot, uint64_t fenceValue) noexcept override
		{
			gassert(slot < c_SwapchainImageCount, "Frame slot out of range");
			m_FenceValues[slot] = fenceValue;
		}

		[[nodiscard]] ICommandAllocator*
		SlotAllocator(uint32_t slot) const noexcept override
		{
			gassert(slot < c_SwapchainImageCount, "Frame slot out of range");
			return m_CommandAllocator[slot].Get();
		}

		[[nodiscard]] TextureHandle
		BackbufferTexture(uint32_t slot) const noexcept override
		{
			gassert(slot < c_SwapchainImageCount, "Frame slot out of range");
			return m_BackBuffers[slot].textureHandle;
		}

		[[nodiscard]] RtvHandle
		BackbufferRtv(uint32_t slot) const noexcept override
		{
			gassert(slot < c_SwapchainImageCount, "Frame slot out of range");
			return m_BackBuffers[slot].rtvHandle;
		}

		[[nodiscard]] DsvHandle
		DepthDsv() const noexcept override
		{
			return m_DepthBuffer.dsvHandle;
		}

		void
		PresentAndAdvance() noexcept override;

		void
		ResizeBackbuffers(uint32_t width, uint32_t height, uint64_t fenceValue) override;

	private:
		void
		CreateSwapchain(HWND hWnd);

		void
		CreateRenderTargets();

		void
		CreateOffscreenRenderTargets();

		void
		DestroyRenderTargets(uint64_t fenceValue);

		DeviceRef          m_Device;
		CommandQueueRef    m_CommandQueue;
		ResourceManagerRef m_ResourceManager;

		bool  m_Headless    = false;
		bool  m_EnableDebug = false;
		void* m_Wnd         = nullptr;
		int   m_Width       = 0;
		int   m_Height      = 0;

		wrl::ComPtr<IDXGISwapChain3> m_SwapChain;

		UINT             m_FrameIndex         = 0;
		UINT             m_LastPresentedIndex = 0;
		TextureRtvHandle m_BackBuffers[c_SwapchainImageCount];
		TextureDsvHandle m_DepthBuffer;
		UINT64           m_FenceValues[c_SwapchainImageCount] = { 0, 0 };

		CommandAllocatorRef m_CommandAllocator[c_SwapchainImageCount];
	};
}
