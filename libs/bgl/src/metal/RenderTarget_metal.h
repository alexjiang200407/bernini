#pragma once
#include "gfx/frame_constants.h"
#include "metal_cpp.h"
#include <bgl/IRenderTarget.h>
#include <core/err/util.h>

#include "gfx/RenderTargetBase.h"
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/constants/constants.h>
#include <bgpu/device/Device.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>

#include <array>
#include <bgl/IGraphics.h>
#include <core/ref/RefCounter.h>
#include <cstdint>

namespace bgl
{
	/**
	 * A render output: a ring of offscreen backbuffers plus depth and motion vectors, owned
	 * independently of Graphics so one renderer can drive many outputs. The frame ring is reached
	 * through RenderTargetBase, so frame-driving code needs neither this type nor Metal.
	 *
	 * A windowed target owns the same ring and blits the finished frame into the layer's drawable
	 * at present. A drawable is transient -- it is acquired per frame and valid only until presented
	 * -- so it cannot back a persistent indexed backbuffer, and the ring stays the renderer's target.
	 * The cost is one full-screen copy per frame.
	 */
	class RenderTarget final : public core::RefCounter<RenderTargetBase>
	{
	public:
		// `desc.wnd` is the CAMetalLayer to present into, and is read only when desc.headless is
		// false. The queue is what the present blit is encoded on.
		RenderTarget(
			const RenderTargetDesc&  desc,
			bgpu::DeviceRef          device,
			bgpu::CommandQueueRef    queue,
			bgpu::ResourceManagerRef resourceManager);

		~RenderTarget() noexcept override;

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
			return m_Layer == nullptr;
		}

		[[nodiscard]] uint64_t
		GetFrameFence(uint32_t frameIndex) const noexcept override
		{
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_FrameFences[frameIndex];
		}

		void
		SetFrameFence(uint32_t frameIndex, uint64_t fenceValue) noexcept override
		{
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			m_FrameFences[frameIndex] = fenceValue;
		}

		[[nodiscard]] bgpu::ICommandAllocator*
		GetFrameAllocator(uint32_t frameIndex) const noexcept override
		{
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_FrameAllocators[frameIndex].Get();
		}

		[[nodiscard]] bgpu::TextureHandle
		GetBackbufferTexture(uint32_t frameIndex) const noexcept override
		{
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_Backbuffers[frameIndex].texture;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetBackbufferRtv(uint32_t frameIndex) const noexcept override
		{
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_Backbuffers[frameIndex].rtv;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetBackbufferSrv(uint32_t frameIndex) const noexcept override
		{
			core::ensure(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_Backbuffers[frameIndex].srv;
		}

		[[nodiscard]] bool
		HasPresented() const noexcept override
		{
			return m_Presented;
		}

		[[nodiscard]] bgpu::DsvHandle
		GetDepthDsv() const noexcept override
		{
			return m_DepthDsv;
		}

		[[nodiscard]] bgpu::TextureHandle
		GetDepthTexture() const noexcept override
		{
			return m_DepthTexture;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetDepthSrv() const noexcept override
		{
			return m_DepthSrv;
		}

		[[nodiscard]] bgpu::TextureHandle
		GetMotionVectorTexture() const noexcept override
		{
			return m_MotionTexture;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetMotionVectorRtv() const noexcept override
		{
			return m_MotionRtv;
		}

		[[nodiscard]] bgpu::TextureHandle
		GetSceneColorTexture() const noexcept override
		{
			return m_SceneColorTexture;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetSceneColorRtv() const noexcept override
		{
			return m_SceneColorRtv;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetSceneColorSrv() const noexcept override
		{
			return m_SceneColorSrv;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetMotionVectorSrv() const noexcept override
		{
			return m_MotionSrv;
		}

		[[nodiscard]] bgpu::TextureHandle
		GetOutlineMaskTexture() const noexcept override
		{
			return m_OutlineMaskTexture;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetOutlineMaskRtv() const noexcept override
		{
			return m_OutlineMaskRtv;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetOutlineMaskSrv() const noexcept override
		{
			return m_OutlineMaskSrv;
		}

		[[nodiscard]] bgpu::TextureHandle
		GetGrassRootHeightTexture() const noexcept override
		{
			return m_GrassRootHeightTexture;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetGrassRootHeightRtv() const noexcept override
		{
			return m_GrassRootHeightRtv;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetGrassRootHeightSrv() const noexcept override
		{
			return m_GrassRootHeightSrv;
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
			return m_History[index].texture;
		}

		[[nodiscard]] bgpu::RtvHandle
		GetHistoryRtv(uint32_t index) const noexcept override
		{
			core::ensure(index < 2, "History index out of range");
			return m_History[index].rtv;
		}

		[[nodiscard]] bgpu::SrvHandle
		GetHistorySrv(uint32_t index) const noexcept override
		{
			core::ensure(index < 2, "History index out of range");
			return m_History[index].srv;
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

	protected:
		void
		ApplyVsync(bool enabled) override;

	private:
		struct Backbuffer
		{
			bgpu::TextureHandle texture;
			bgpu::RtvHandle     rtv;
			bgpu::SrvHandle     srv;
		};

		struct Accumulation
		{
			bgpu::TextureHandle texture;
			bgpu::RtvHandle     rtv;
			bgpu::SrvHandle     srv;
		};

		void
		CreateAttachments();

		// Split by which size owns them: a render scale rebuilds only the render half, where a
		// resize rebuilds both.
		void
		CreateOutputAttachments();

		void
		CreateRenderAttachments();

		// Frees every texture and view the ring owns. Immediate, not deferred: the caller has
		// already idled the GPU for this target, which is the precondition ResizeBackbuffers states.
		void
		ReleaseAttachments() noexcept;

		void
		ReleaseOutputAttachments() noexcept;

		void
		ReleaseRenderAttachments() noexcept;

		// Blits the frame just recorded into the layer's next drawable and presents it. Null layer
		// (headless) is a no-op.
		void
		PresentToLayer() noexcept;

		bgpu::DeviceRef          m_Device;
		bgpu::CommandQueueRef    m_Queue;
		bgpu::ResourceManagerRef m_ResourceManager;

		// Windowed only: the present blit's own list, recorded once per present.
		bgpu::CommandAllocatorRef m_PresentAllocator;
		bgpu::CommandListRef      m_PresentList;

		// Borrowed: the window system owns the layer and outlives the target.
		CA::MetalLayer* m_Layer = nullptr;

		bool m_TaaEnabled     = false;
		bool m_OutlineEnabled = true;
		bool m_TaaAllocated   = false;

		std::array<Backbuffer, c_SwapchainImageCount>                m_Backbuffers;
		std::array<uint64_t, c_SwapchainImageCount>                  m_FrameFences{};
		std::array<bgpu::CommandAllocatorRef, c_SwapchainImageCount> m_FrameAllocators;

		bgpu::TextureHandle m_DepthTexture;
		bgpu::DsvHandle     m_DepthDsv;
		bgpu::SrvHandle     m_DepthSrv;
		bgpu::TextureHandle m_MotionTexture;
		bgpu::RtvHandle     m_MotionRtv;
		bgpu::TextureHandle m_SceneColorTexture;
		bgpu::RtvHandle     m_SceneColorRtv;
		bgpu::SrvHandle     m_SceneColorSrv;
		bgpu::SrvHandle     m_MotionSrv;
		bgpu::TextureHandle m_OutlineMaskTexture;
		bgpu::RtvHandle     m_OutlineMaskRtv;
		bgpu::SrvHandle     m_OutlineMaskSrv;
		bgpu::TextureHandle m_GrassRootHeightTexture;
		bgpu::RtvHandle     m_GrassRootHeightRtv;
		bgpu::SrvHandle     m_GrassRootHeightSrv;

		// Allocated only when m_TaaAllocated; a target that never resolves pays neither the memory nor
		// the two RTV slots.
		std::array<Accumulation, 2> m_History;
		uint32_t                    m_CurrentHistoryIndex = 0;
		bool                        m_HistoryValid        = false;

		uint32_t m_FrameIndex         = 0;
		uint32_t m_LastPresentedIndex = 0;
		bool     m_Presented          = false;
	};
}
