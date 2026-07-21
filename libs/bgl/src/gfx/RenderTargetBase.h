#pragma once
#include "cmd/CommandAllocator.h"
#include "resource/Dsv.h"
#include "resource/Rtv.h"
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

		/** The frame-in-flight index the next frame records into. */
		[[nodiscard]] virtual uint32_t
		FrameIndex() const noexcept = 0;

		/** The index the last presented frame landed in; what a readback must sample. */
		[[nodiscard]] virtual uint32_t
		LastPresentedIndex() const noexcept = 0;

		[[nodiscard]] virtual bool
		IsHeadless() const noexcept = 0;

		/** Zero when no frame has used the index yet, so nothing needs waiting on. */
		[[nodiscard]] virtual uint64_t
		FrameFence(uint32_t frameIndex) const noexcept = 0;

		virtual void
		SetFrameFence(uint32_t frameIndex, uint64_t fenceValue) noexcept = 0;

		/** Borrowed; the target owns it. Reset it before recording the frame. */
		[[nodiscard]] virtual ICommandAllocator*
		FrameAllocator(uint32_t frameIndex) const noexcept = 0;

		[[nodiscard]] virtual TextureHandle
		BackbufferTexture(uint32_t frameIndex) const noexcept = 0;

		[[nodiscard]] virtual RtvHandle
		BackbufferRtv(uint32_t frameIndex) const noexcept = 0;

		[[nodiscard]] virtual DsvHandle
		DepthDsv() const noexcept = 0;

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
	};
}
