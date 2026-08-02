#pragma once
#include "metal_cpp.h"

#include "cmd/CommandAllocator.h"
#include "cmd/CommandQueue.h"
#include "constants/constants.h"
#include "device/Device.h"
#include "gfx/RenderTargetBase.h"
#include "resource/ResourceManager.h"

#include <core/ref/RefCounter.h>

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
			const RenderTargetDesc& desc,
			DeviceRef               device,
			CommandQueueRef         queue,
			ResourceManagerRef      resourceManager);

		~RenderTarget() noexcept override;

		[[nodiscard]] uint32_t
		GetWidth() const noexcept override
		{
			return m_Width;
		}

		[[nodiscard]] uint32_t
		GetHeight() const noexcept override
		{
			return m_Height;
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
			return m_Layer == nullptr;
		}

		[[nodiscard]] uint64_t
		GetFrameFence(uint32_t frameIndex) const noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_FrameFences[frameIndex];
		}

		void
		SetFrameFence(uint32_t frameIndex, uint64_t fenceValue) noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			m_FrameFences[frameIndex] = fenceValue;
		}

		[[nodiscard]] ICommandAllocator*
		GetFrameAllocator(uint32_t frameIndex) const noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_FrameAllocators[frameIndex].Get();
		}

		[[nodiscard]] TextureHandle
		GetBackbufferTexture(uint32_t frameIndex) const noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_Backbuffers[frameIndex].texture;
		}

		[[nodiscard]] RtvHandle
		GetBackbufferRtv(uint32_t frameIndex) const noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_Backbuffers[frameIndex].rtv;
		}

		[[nodiscard]] DsvHandle
		GetDepthDsv() const noexcept override
		{
			return m_DepthDsv;
		}

		[[nodiscard]] TextureHandle
		GetMotionVectorTexture() const noexcept override
		{
			return m_MotionTexture;
		}

		[[nodiscard]] RtvHandle
		GetMotionVectorRtv() const noexcept override
		{
			return m_MotionRtv;
		}

		[[nodiscard]] TextureHandle
		GetSceneColorTexture() const noexcept override
		{
			return m_SceneColorTexture;
		}

		[[nodiscard]] RtvHandle
		GetSceneColorRtv() const noexcept override
		{
			return m_SceneColorRtv;
		}

		[[nodiscard]] SrvHandle
		GetSceneColorSrv() const noexcept override
		{
			return m_SceneColorSrv;
		}

		[[nodiscard]] bool
		IsTaaEnabled() const noexcept override
		{
			return m_TaaEnabled;
		}

		void
		PresentAndAdvance() noexcept override;

		void
		ResizeBackbuffers(uint32_t width, uint32_t height) override;

	private:
		struct Backbuffer
		{
			TextureHandle texture;
			RtvHandle     rtv;
		};

		void
		CreateAttachments();

		// Frees every texture and view the ring owns. Immediate, not deferred: the caller has
		// already idled the GPU for this target, which is the precondition ResizeBackbuffers states.
		void
		ReleaseAttachments() noexcept;

		// Blits the frame just recorded into the layer's next drawable and presents it. Null layer
		// (headless) is a no-op.
		void
		PresentToLayer() noexcept;

		DeviceRef          m_Device;
		CommandQueueRef    m_Queue;
		ResourceManagerRef m_ResourceManager;

		// Borrowed: the window system owns the layer and outlives the target.
		CA::MetalLayer* m_Layer = nullptr;

		uint32_t m_Width      = 0;
		uint32_t m_Height     = 0;
		bool     m_TaaEnabled = false;

		std::array<Backbuffer, c_SwapchainImageCount>          m_Backbuffers;
		std::array<uint64_t, c_SwapchainImageCount>            m_FrameFences{};
		std::array<CommandAllocatorRef, c_SwapchainImageCount> m_FrameAllocators;

		TextureHandle m_DepthTexture;
		DsvHandle     m_DepthDsv;
		TextureHandle m_MotionTexture;
		RtvHandle     m_MotionRtv;
		TextureHandle m_SceneColorTexture;
		RtvHandle     m_SceneColorRtv;
		SrvHandle     m_SceneColorSrv;

		uint32_t m_FrameIndex         = 0;
		uint32_t m_LastPresentedIndex = 0;
	};
}
