#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandQueue.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "resource/Dsv.h"
#include "resource/ResourceManager.h"
#include "resource/Rtv.h"
#include <bgl/IRenderTarget.h>
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
	 * outputs. Graphics is a friend: it drives a frame against the bound target using
	 * this target's backbuffers / fences / allocators.
	 */
	class RenderTarget : public core::RefCounter<IRenderTarget>
	{
	public:
		RenderTarget(
			const RenderTargetDesc& desc,
			DeviceHandle            device,
			CommandQueueHandle      queue,
			ResourceManagerHandle   resourceManager,
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

	private:
		friend class Graphics;

		void
		CreateSwapchain(HWND hWnd);

		void
		CreateRenderTargets();

		void
		CreateOffscreenRenderTargets();

		void
		DestroyRenderTargets(uint64_t fenceValue);

		DeviceHandle          m_Device;
		CommandQueueHandle    m_CommandQueue;
		ResourceManagerHandle m_ResourceManager;

		bool  m_Headless    = false;
		bool  m_EnableDebug = false;
		void* m_Wnd         = nullptr;
		int   m_Width       = 0;
		int   m_Height      = 0;

		wrl::ComPtr<IDXGISwapChain3> m_SwapChain;

		UINT             m_FrameIndex         = 0;
		UINT             m_LastPresentedIndex = 0;
		TextureRtvHandle m_BackBuffers[c_BufferCount];
		TextureDsvHandle m_DepthBuffer;
		UINT64           m_FenceValues[c_BufferCount] = { 0, 0 };

		CommandAllocatorHandle m_CommandAllocator[c_BufferCount];
	};
}
