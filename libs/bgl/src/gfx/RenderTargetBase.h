#pragma once
#include "cmd/CommandAllocator.h"
#include "resource/Dsv.h"
#include "resource/Rtv.h"
#include "resource/Srv.h"
#include "resource/Texture.h"
#include <bgl/IRenderTarget.h>

namespace bgl
{
	/**
	 * The per-frame ring a renderer drives a target through: one backbuffer, one fence value and
	 * one command allocator per frame in flight, plus present and resize.
	 *
	 * It exists so frame-driving code needs no backend type. `IRenderTarget` is the public API and
	 * must not carry allocators or fence values; the concrete target is backend-specific and cannot
	 * be named outside its own translation unit. This is the seam between them, mirroring
	 * GraphicsBase.
	 *
	 * Frame indices run to `c_SwapchainImageCount`; passing anything else is a precondition
	 * violation, not a runtime error. "Frame index" here is the backbuffer/frame-in-flight index
	 * the target already tracks -- unrelated to `core::slot_vector`, and unrelated to the
	 * Graphics-wide GPU-debug readback ring, whose entries docs/gfx_debug.md calls slots.
	 */
	class RenderTargetBase : public IRenderTarget
	{
	public:
		RenderTargetBase(const RenderTargetBase&) noexcept = delete;
		RenderTargetBase(RenderTargetBase&&) noexcept      = delete;

		RenderTargetBase&
		operator=(const RenderTargetBase&) noexcept = delete;

		RenderTargetBase&
		operator=(RenderTargetBase&&) noexcept = delete;

		[[nodiscard]] uint32_t
		GetWidth() const noexcept final
		{
			return m_Width;
		}

		[[nodiscard]] uint32_t
		GetHeight() const noexcept final
		{
			return m_Height;
		}

		/** The frame-in-flight index the next frame records into. */
		[[nodiscard]] virtual uint32_t
		GetFrameIndex() const noexcept = 0;

		/** The index the last presented frame landed in; what a readback must sample. */
		[[nodiscard]] virtual uint32_t
		GetLastPresentedIndex() const noexcept = 0;

		[[nodiscard]] virtual bool
		IsHeadless() const noexcept = 0;

		/** Zero when no frame has used the index yet, so nothing needs waiting on. */
		[[nodiscard]] virtual uint64_t
		GetFrameFence(uint32_t frameIndex) const noexcept = 0;

		virtual void
		SetFrameFence(uint32_t frameIndex, uint64_t fenceValue) noexcept = 0;

		/** Borrowed; the target owns it. Reset it before recording the frame. */
		[[nodiscard]] virtual ICommandAllocator*
		GetFrameAllocator(uint32_t frameIndex) const noexcept = 0;

		[[nodiscard]] virtual TextureHandle
		GetBackbufferTexture(uint32_t frameIndex) const noexcept = 0;

		[[nodiscard]] virtual RtvHandle
		GetBackbufferRtv(uint32_t frameIndex) const noexcept = 0;

		[[nodiscard]] virtual DsvHandle
		GetDepthDsv() const noexcept = 0;

		[[nodiscard]] virtual TextureHandle
		GetDepthTexture() const noexcept = 0;

		/**
		 * A shader-readable view of the depth attachment, for passes that consume the scene's depth
		 * after the geometry has written it. Reads raw device depth; linearizing it is the
		 * consumer's business, since only it knows which projection produced the frame.
		 */
		[[nodiscard]] virtual SrvHandle
		GetDepthSrv() const noexcept = 0;

		/**
		 * The screen-space velocity buffer the forward pass writes alongside colour: for each pixel,
		 * the UV displacement from where its surface was last frame to where it is now, so history is
		 * sampled at `uv - motion`. Cleared to zero, so a pixel nothing drew reads as static.
		 *
		 * One texture, not one per frame in flight: a consumer wants this frame's motion, and the
		 * history it reprojects into is its own resource.
		 */
		[[nodiscard]] virtual TextureHandle
		GetMotionVectorTexture() const noexcept = 0;

		[[nodiscard]] virtual RtvHandle
		GetMotionVectorRtv() const noexcept = 0;

		/**
		 * The linear HDR colour every geometry pass renders into, ahead of the tonemap that writes
		 * the backbuffer. Exposure is already applied; the display curve is not.
		 *
		 * One texture, not one per frame in flight: the tonemap consumes it within the frame that
		 * wrote it, so a second copy would never be read.
		 */
		[[nodiscard]] virtual TextureHandle
		GetSceneColorTexture() const noexcept = 0;

		[[nodiscard]] virtual RtvHandle
		GetSceneColorRtv() const noexcept = 0;

		[[nodiscard]] virtual SrvHandle
		GetSceneColorSrv() const noexcept = 0;

		[[nodiscard]] virtual SrvHandle
		GetMotionVectorSrv() const noexcept = 0;

		/**
		 * The R8 coverage mask the outline-mask pass draws the outlined submesh instances into,
		 * and the post-process dilates into the outline. Cleared to zero each frame; sized with
		 * the target like every other attachment.
		 */
		[[nodiscard]] virtual TextureHandle
		GetOutlineMaskTexture() const noexcept = 0;

		[[nodiscard]] virtual RtvHandle
		GetOutlineMaskRtv() const noexcept = 0;

		[[nodiscard]] virtual SrvHandle
		GetOutlineMaskSrv() const noexcept = 0;

		/**
		 * The two accumulation buffers TAA ping-pongs between: index `GetCurrentHistoryIndex()` is the one
		 * this frame's resolve writes, the other is the one it reads. Null on a target without TAA,
		 * which allocates neither.
		 *
		 * @pre `index` is 0 or 1.
		 */
		[[nodiscard]] virtual TextureHandle
		GetHistoryTexture(uint32_t index) const noexcept = 0;

		[[nodiscard]] virtual RtvHandle
		GetHistoryRtv(uint32_t index) const noexcept = 0;

		[[nodiscard]] virtual SrvHandle
		GetHistorySrv(uint32_t index) const noexcept = 0;

		[[nodiscard]] virtual uint32_t
		GetCurrentHistoryIndex() const noexcept = 0;

		/**
		 * False until a resolve has written a history this target can reproject from -- at creation
		 * and again after a resize, which discards the accumulation rather than rescaling it. The
		 * frame that sees it false takes the scene colour whole instead of blending.
		 */
		[[nodiscard]] virtual bool
		IsHistoryValid() const noexcept = 0;

		/** Swaps which history the next frame writes, and marks the one just written readable. */
		virtual void
		AdvanceHistory() noexcept = 0;

		/**
		 * How many frames have begun on this target. What the jitter sequence and the alpha hash
		 * seed advance on: per target, like the history ping-pong, so a target drawn every other
		 * frame -- two viewports sharing a renderer -- still walks the whole sequence rather than
		 * every second term of it.
		 */
		[[nodiscard]] uint64_t
		GetFrameCount() const noexcept
		{
			return m_FrameCount;
		}

		void
		AdvanceFrameCount() noexcept
		{
			++m_FrameCount;
		}

		/**
		 * Presents the frame just submitted, then advances to the index the next one records into.
		 * A headless target presents nothing and advances round-robin; a windowed one takes the
		 * index back from its swapchain.
		 *
		 * @pre the frame's command list has been submitted and its fence recorded through
		 *      SetFrameFence, or the presented backbuffer may still be being written.
		 */
		virtual void
		PresentAndAdvance() noexcept = 0;

		/**
		 * Releases the backbuffers and depth and recreates them at the new size, resetting the
		 * frame ring: every fence returns to zero, since the backbuffers they described are gone.
		 *
		 * @pre the GPU is idle for this target -- nothing in flight may still reference the
		 *      backbuffers, and no command list may hold a reference either.
		 */
		virtual void
		ResizeBackbuffers(uint32_t width, uint32_t height) = 0;

	protected:
		RenderTargetBase() noexcept = default;

		/**
		 * Records the size every attachment is allocated at. Called from a backend's constructor
		 * before it creates them, and again from its `ResizeBackbuffers`.
		 *
		 * @pre both dimensions are non-zero.
		 */
		void
		SetSize(uint32_t width, uint32_t height) noexcept
		{
			gassert(width > 0 && height > 0, "A render target cannot be zero-sized");

			m_Width  = width;
			m_Height = height;
		}

	private:
		uint32_t m_Width      = 0;
		uint32_t m_Height     = 0;
		uint64_t m_FrameCount = 0;
	};
}
