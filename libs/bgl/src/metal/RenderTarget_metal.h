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
	 * Headless only for now. A windowed target draws into a CAMetalLayer's drawable, which is a
	 * different acquire/present shape than a ring the target owns outright, and it lands with the
	 * SDL examples.
	 */
	class RenderTarget final : public core::RefCounter<RenderTargetBase>
	{
	public:
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
		FrameIndex() const noexcept override
		{
			return m_FrameIndex;
		}

		[[nodiscard]] uint32_t
		LastPresentedIndex() const noexcept override
		{
			return m_LastPresentedIndex;
		}

		[[nodiscard]] bool
		IsHeadless() const noexcept override
		{
			return true;
		}

		[[nodiscard]] uint64_t
		FrameFence(uint32_t frameIndex) const noexcept override
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
		FrameAllocator(uint32_t frameIndex) const noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_FrameAllocators[frameIndex].Get();
		}

		[[nodiscard]] TextureHandle
		BackbufferTexture(uint32_t frameIndex) const noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_Backbuffers[frameIndex].texture;
		}

		[[nodiscard]] RtvHandle
		BackbufferRtv(uint32_t frameIndex) const noexcept override
		{
			gassert(frameIndex < c_SwapchainImageCount, "Frame index out of range");
			return m_Backbuffers[frameIndex].rtv;
		}

		[[nodiscard]] DsvHandle
		DepthDsv() const noexcept override
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

		DeviceRef          m_Device;
		CommandQueueRef    m_Queue;
		ResourceManagerRef m_ResourceManager;

		uint32_t m_Width  = 0;
		uint32_t m_Height = 0;

		std::array<Backbuffer, c_SwapchainImageCount>          m_Backbuffers;
		std::array<uint64_t, c_SwapchainImageCount>            m_FrameFences{};
		std::array<CommandAllocatorRef, c_SwapchainImageCount> m_FrameAllocators;

		TextureHandle m_DepthTexture;
		DsvHandle     m_DepthDsv;
		TextureHandle m_MotionTexture;
		RtvHandle     m_MotionRtv;

		uint32_t m_FrameIndex         = 0;
		uint32_t m_LastPresentedIndex = 0;
	};
}
