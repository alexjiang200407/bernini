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

	struct TextureRtvSrvHandle
	{
		TextureHandle textureHandle;
		RtvHandle     rtvHandle;
		SrvHandle     srvHandle;
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
		GetFrameIndex() const noexcept override
		{
			return m_FrameIndex;
		}

		[[nodiscard]] uint32_t
		GetLastPresentedIndex() const noexcept override
		{
			return m_LastPresentedIndex;
		}

		[[nodiscard]] bool
		IsHeadless() const noexcept override
		{
			return m_Headless;
		}

		[[nodiscard]] uint64_t
		GetFrameFence(uint32_t frameIndex) const noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_FenceValues[frameIndex];
		}

		void
		SetFrameFence(uint32_t frameIndex, uint64_t fenceValue) noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			m_FenceValues[frameIndex] = fenceValue;
		}

		[[nodiscard]] ICommandAllocator*
		GetFrameAllocator(uint32_t frameIndex) const noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_CommandAllocator[frameIndex].Get();
		}

		[[nodiscard]] TextureHandle
		GetBackbufferTexture(uint32_t frameIndex) const noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_BackBuffers[frameIndex].textureHandle;
		}

		[[nodiscard]] RtvHandle
		GetBackbufferRtv(uint32_t frameIndex) const noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_BackBuffers[frameIndex].rtvHandle;
		}

		[[nodiscard]] DsvHandle
		GetDepthDsv() const noexcept override
		{
			return m_DepthBuffer.dsvHandle;
		}

		[[nodiscard]] TextureHandle
		GetMotionVectorTexture() const noexcept override
		{
			return m_MotionVectors.textureHandle;
		}

		[[nodiscard]] RtvHandle
		GetMotionVectorRtv() const noexcept override
		{
			return m_MotionVectors.rtvHandle;
		}

		[[nodiscard]] TextureHandle
		GetSceneColorTexture() const noexcept override
		{
			return m_SceneColor.textureHandle;
		}

		[[nodiscard]] RtvHandle
		GetSceneColorRtv() const noexcept override
		{
			return m_SceneColor.rtvHandle;
		}

		[[nodiscard]] SrvHandle
		GetSceneColorSrv() const noexcept override
		{
			return m_SceneColor.srvHandle;
		}

		[[nodiscard]] SrvHandle
		GetMotionVectorSrv() const noexcept override
		{
			return m_MotionVectorSrv;
		}

		[[nodiscard]] bool
		IsTaaEnabled() const noexcept override
		{
			return m_TaaEnabled;
		}

		[[nodiscard]] TextureHandle
		GetHistoryTexture(uint32_t index) const noexcept override
		{
			gassert(index < 2, "History index out of range");
			return m_History[index].textureHandle;
		}

		[[nodiscard]] RtvHandle
		GetHistoryRtv(uint32_t index) const noexcept override
		{
			gassert(index < 2, "History index out of range");
			return m_History[index].rtvHandle;
		}

		[[nodiscard]] SrvHandle
		GetHistorySrv(uint32_t index) const noexcept override
		{
			gassert(index < 2, "History index out of range");
			return m_History[index].srvHandle;
		}

		[[nodiscard]] uint32_t
		GetHistoryIndex() const noexcept override
		{
			return m_HistoryIndex;
		}

		[[nodiscard]] bool
		IsHistoryValid() const noexcept override
		{
			return m_HistoryValid;
		}

		void
		AdvanceHistory() noexcept override
		{
			m_HistoryValid = true;
			m_HistoryIndex ^= 1u;
		}

		void
		PresentAndAdvance() noexcept override;

		void
		ResizeBackbuffers(uint32_t width, uint32_t height) override;

	private:
		void
		CreateSwapchain(HWND hWnd);

		void
		CreateRenderTargets();

		void
		CreateOffscreenRenderTargets();

		void
		CreateAttachments();

		void
		DestroyRenderTargets();

		DeviceRef          m_Device;
		CommandQueueRef    m_CommandQueue;
		ResourceManagerRef m_ResourceManager;

		bool  m_Headless    = false;
		bool  m_TaaEnabled  = false;
		bool  m_EnableDebug = false;
		void* m_Wnd         = nullptr;
		int   m_Width       = 0;
		int   m_Height      = 0;

		wrl::ComPtr<IDXGISwapChain3> m_SwapChain;

		UINT                m_FrameIndex         = 0;
		UINT                m_LastPresentedIndex = 0;
		TextureRtvHandle    m_BackBuffers[c_SwapchainImageCount];
		TextureDsvHandle    m_DepthBuffer;
		TextureRtvHandle    m_MotionVectors;
		TextureRtvSrvHandle m_SceneColor;
		SrvHandle           m_MotionVectorSrv;

		// Allocated only when m_TaaEnabled; a target that never resolves pays neither the memory nor
		// the two RTV slots.
		std::array<TextureRtvSrvHandle, 2> m_History;
		uint32_t                           m_HistoryIndex                       = 0;
		bool                               m_HistoryValid                       = false;
		UINT64                             m_FenceValues[c_SwapchainImageCount] = { 0, 0 };

		CommandAllocatorRef m_CommandAllocator[c_SwapchainImageCount];
	};
}
