#pragma once
#include "gfx/RenderTargetBase.h"
#include "gfx/frame_constants.h"
#include <bgl/IGraphics.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/constants/constants.h>
#include <bgpu/device/Device.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <core/err/util.h>
#include <core/ref/RefCounter.h>

namespace bgl
{
	struct TextureRtvHandle
	{
		bgpu::TextureHandle textureHandle;
		bgpu::RtvHandle     rtvHandle;
	};

	struct TextureRtvSrvHandle
	{
		bgpu::TextureHandle textureHandle;
		bgpu::RtvHandle     rtvHandle;
		bgpu::SrvHandle     srvHandle;
	};

	struct TextureDsvHandle
	{
		bgpu::TextureHandle textureHandle;
		bgpu::DsvHandle     dsvHandle;
		bgpu::SrvHandle     srvHandle;
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
			const RenderTargetDesc&  desc,
			bgpu::DeviceRef          device,
			bgpu::CommandQueueRef    queue,
			bgpu::ResourceManagerRef resourceManager,
			bool                     enableDebug);

		~RenderTarget() noexcept override;

		RenderTarget(const RenderTarget&) noexcept = delete;
		RenderTarget(RenderTarget&&) noexcept      = delete;

		RenderTarget&
		operator=(const RenderTarget&) noexcept = delete;

		RenderTarget&
		operator=(RenderTarget&&) noexcept = delete;

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
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_FenceValues[frameIndex];
		}

		void
		SetFrameFence(uint32_t frameIndex, uint64_t fenceValue) noexcept override
		{
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			m_FenceValues[frameIndex] = fenceValue;
		}

		[[nodiscard]] bgpu::ICommandAllocator*
		GetFrameAllocator(uint32_t frameIndex) const noexcept override
		{
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_CommandAllocator[frameIndex].Get();
		}

		[[nodiscard]] bgpu::TextureHandle
		GetBackbufferTexture(uint32_t frameIndex) const noexcept override
		{
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_BackBuffers[frameIndex].textureHandle;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetBackbufferRtv(uint32_t frameIndex) const noexcept override
		{
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_BackBuffers[frameIndex].rtvHandle;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetBackbufferSrv(uint32_t frameIndex) const noexcept override
		{
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_BackBuffers[frameIndex].srvHandle;
		}

		[[nodiscard]] bool
		HasPresented() const noexcept override
		{
			return m_Presented;
		}

		[[nodiscard]] bgpu::DsvHandle
		GetDepthDsv() const noexcept override
		{
			return m_DepthBuffer.dsvHandle;
		}

		[[nodiscard]] bgpu::TextureHandle
		GetDepthTexture() const noexcept override
		{
			return m_DepthBuffer.textureHandle;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetDepthSrv() const noexcept override
		{
			return m_DepthBuffer.srvHandle;
		}

		[[nodiscard]] bgpu::TextureHandle
		GetMotionVectorTexture() const noexcept override
		{
			return m_MotionVectors.textureHandle;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetMotionVectorRtv() const noexcept override
		{
			return m_MotionVectors.rtvHandle;
		}

		[[nodiscard]] bgpu::TextureHandle
		GetSceneColorTexture() const noexcept override
		{
			return m_SceneColor.textureHandle;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetSceneColorRtv() const noexcept override
		{
			return m_SceneColor.rtvHandle;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetSceneColorSrv() const noexcept override
		{
			return m_SceneColor.srvHandle;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetMotionVectorSrv() const noexcept override
		{
			return m_MotionVectorSrv;
		}

		[[nodiscard]] bgpu::TextureHandle
		GetOutlineMaskTexture() const noexcept override
		{
			return m_OutlineMask.textureHandle;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetOutlineMaskRtv() const noexcept override
		{
			return m_OutlineMask.rtvHandle;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetOutlineMaskSrv() const noexcept override
		{
			return m_OutlineMask.srvHandle;
		}

		[[nodiscard]] bool
		IsTaaEnabled() const noexcept override
		{
			return m_TaaEnabled;
		}

		[[nodiscard]] bool
		IsOutlineEnabled() const noexcept override
		{
			return m_OutlineEnabled;
		}

		void
		SetOutlineEnabled(bool enabled) noexcept override
		{
			m_OutlineEnabled = enabled;
		}

		void
		SetTaaEnabled(bool enabled) override
		{
			if (enabled && !m_TaaAllocated)
			{
				throw GraphicsError(
					"SetTaaEnabled(true) on a render target created without "
					"RenderTargetDesc::taaEnabled: it has no history to accumulate into");
			}

			// Discarded rather than paused: the frames the accumulation would have to bridge were
			// never rendered, so reprojecting across the gap would blend in a stale image.
			if (!enabled)
			{
				m_HistoryValid = false;
			}

			m_TaaEnabled = enabled;
		}

		[[nodiscard]] bgpu::TextureHandle
		GetHistoryTexture(uint32_t index) const noexcept override
		{
			core::ensure(index < 2, "History index out of range");
			return m_History[index].textureHandle;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetHistoryRtv(uint32_t index) const noexcept override
		{
			core::ensure(index < 2, "History index out of range");
			return m_History[index].rtvHandle;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetHistorySrv(uint32_t index) const noexcept override
		{
			core::ensure(index < 2, "History index out of range");
			return m_History[index].srvHandle;
		}

		[[nodiscard]] uint32_t
		GetCurrentHistoryIndex() const noexcept override
		{
			return m_CurrentHistoryIndex;
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
			m_CurrentHistoryIndex ^= 1u;
		}

		void
		PresentAndAdvance() noexcept override;

		void
		ResizeBackbuffers(uint32_t width, uint32_t height) override;

		void
		SetRenderScale(float scale) override;

	private:
		void
		CreateSwapchain(HWND hWnd);

		// Rebuilds every backbuffer handle and attachment against the sizes now recorded, and
		// resets the frame ring -- the half a resize and a scale change have in common.
		void
		RecreateRenderTargets();

		void
		CreateRenderTargets();

		void
		CreateOffscreenRenderTargets();

		void
		CreateAttachments();

		// Split by which size owns them: a render scale rebuilds only the render half, where a
		// resize rebuilds both.
		void
		CreateRenderAttachments();

		void
		CreateHistoryAttachments();

		void
		DestroyRenderAttachments();

		void
		DestroyHistoryAttachments();

		void
		DestroyRenderTargets();

		bgpu::DeviceRef          m_Device;
		bgpu::CommandQueueRef    m_CommandQueue;
		bgpu::ResourceManagerRef m_ResourceManager;

		bool  m_Headless       = false;
		bool  m_TaaEnabled     = false;
		bool  m_OutlineEnabled = true;
		bool  m_TaaAllocated   = false;
		bool  m_EnableDebug    = false;
		void* m_Wnd            = nullptr;

		wrl::ComPtr<IDXGISwapChain3> m_SwapChain;

		UINT                m_FrameIndex         = 0;
		UINT                m_LastPresentedIndex = 0;
		bool                m_Presented          = false;
		TextureRtvSrvHandle m_BackBuffers[c_SwapchainImageCount];
		TextureDsvHandle    m_DepthBuffer;
		TextureRtvHandle    m_MotionVectors;
		TextureRtvSrvHandle m_SceneColor;
		bgpu::SrvHandle     m_MotionVectorSrv;
		TextureRtvSrvHandle m_OutlineMask;

		// Allocated only when m_TaaAllocated; a target that never resolves pays neither the memory nor
		// the two RTV slots.
		std::array<TextureRtvSrvHandle, 2> m_History;
		uint32_t                           m_CurrentHistoryIndex                = 0;
		bool                               m_HistoryValid                       = false;
		UINT64                             m_FenceValues[c_SwapchainImageCount] = { 0, 0 };

		bgpu::CommandAllocatorRef m_CommandAllocator[c_SwapchainImageCount];
	};
}
