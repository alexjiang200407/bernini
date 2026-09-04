#pragma once
#include "cmd/CommandAllocator.h"
#include "resource/Dsv.h"
#include "resource/Rtv.h"
#include "resource/Srv.h"
#include "resource/Texture.h"
#include <algorithm>
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <cmath>
#include <cstdint>

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

		[[nodiscard]] uint32_t
		GetRenderWidth() const noexcept final
		{
			return m_RenderWidth;
		}

		[[nodiscard]] uint32_t
		GetRenderHeight() const noexcept final
		{
			return m_RenderHeight;
		}

		[[nodiscard]] float
		GetRenderScale() const noexcept
		{
			return m_RenderScale;
		}

		[[nodiscard]] float
		GetTaaReconstructionWidth() const noexcept final
		{
			return m_TaaReconstructionWidth;
		}

		void
		SetTaaReconstructionWidth(float width) final
		{
			if (!(width > 0.0f) || !std::isfinite(width))
			{
				throw GraphicsError(
					"RenderTargetDesc::taaReconstructionWidth must be positive and finite");
			}

			m_TaaReconstructionWidth = width;
		}

		/**
		 * Re-derives the render size and recreates every attachment sized by it. The output size,
		 * the swapchain and the frame ring are untouched; the accumulation is discarded, since a
		 * history gathered on one render grid describes samples the new one does not take.
		 *
		 * @pre the GPU is idle for this target.
		 * @throws GraphicsError if `scale` is not a positive, finite number.
		 */
		virtual void
		SetRenderScale(float scale) = 0;

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

		// A shader-readable view of a ring slot, for a pass on another target that samples what this
		// one presented. Null on a D3D12 windowed target, whose swapchain images get no view; a
		// windowed target's ring is never read on either backend.
		[[nodiscard]] virtual SrvHandle
		GetBackbufferSrv(uint32_t frameIndex) const noexcept = 0;

		// Whether a frame has been presented since creation or the last resize; before that the
		// ring holds no content.
		[[nodiscard]] virtual bool
		HasPresented() const noexcept = 0;

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
		 * and the post-process dilates into the outline. Cleared to zero each frame; sized with the
		 * render grid, like every other attachment a geometry pass draws into.
		 */
		[[nodiscard]] virtual TextureHandle
		GetOutlineMaskTexture() const noexcept = 0;

		[[nodiscard]] virtual RtvHandle
		GetOutlineMaskRtv() const noexcept = 0;

		[[nodiscard]] virtual SrvHandle
		GetOutlineMaskSrv() const noexcept = 0;

		/**
		 * The two accumulation buffers TAA ping-pongs between: index `GetCurrentHistoryIndex()` is the one
		 * this frame's resolve writes, the other is the one it reads. Sized with the *output* grid,
		 * which is what the resolve reconstructs onto. Null on a target without TAA, which allocates
		 * neither.
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
		 * Records the output size every attachment is allocated from, and re-derives the render
		 * size. Called from a backend's constructor before it creates them, and again from its
		 * `ResizeBackbuffers` and `SetRenderScale`.
		 *
		 * @throws GraphicsError if either dimension is zero, or `scale` is not a positive, finite
		 *         number. Both arrive from the caller unchecked -- `RenderTargetDesc` on the way in
		 *         and `IGraphics::Resize` afterwards -- so this is the only place they are validated.
		 */
		void
		SetSize(uint32_t width, uint32_t height, float scale)
		{
			if (width == 0 || height == 0)
			{
				throw GraphicsError("A render target cannot be zero-sized");
			}

			if (!(scale > 0.0f) || !std::isfinite(scale))
			{
				throw GraphicsError("RenderTargetDesc::renderScale must be positive and finite");
			}

			m_Width       = width;
			m_Height      = height;
			m_RenderScale = scale;

			// Floored at a pixel: a narrow window under a small scale would otherwise ask for a
			// zero-sized attachment, which every backend rejects.
			m_RenderWidth =
				std::max(1u, static_cast<uint32_t>(std::lround(static_cast<float>(width) * scale)));
			m_RenderHeight = std::max(
				1u,
				static_cast<uint32_t>(std::lround(static_cast<float>(height) * scale)));
		}

	private:
		uint32_t m_Width        = 0;
		uint32_t m_Height       = 0;
		uint32_t m_RenderWidth  = 0;
		uint32_t m_RenderHeight = 0;
		float    m_RenderScale  = 1.0f;
		uint64_t m_FrameCount   = 0;

		// Not backend state: nothing is allocated from it, so it needs neither an override nor a
		// GPU idle to change.
		float m_TaaReconstructionWidth = RenderTargetDesc().taaReconstructionWidth;
	};
}
