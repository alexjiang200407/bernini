#pragma once
#include <assetlib_structs/ImageData.h>
#include <bgl/IGpuAssertionHandler.h>
#include <bgl/IRenderTarget.h>
#include <bgl/RenderJob.h>
#include <bgl/api.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	/**
	 * Names one in-flight backbuffer capture. Minted by SubmitCapture; spent by the
	 * TryResolveCapture that returns its image, or by DiscardCapture.
	 */
	struct CaptureTicket
	{
		uint64_t id = 0;

		[[nodiscard]] bool
		IsValid() const noexcept
		{
			return id != 0;
		}
	};

	/**
	 * One independent submission context over the shared device: its own command queue, recorder
	 * and frame state, so work submitted here neither blocks nor is blocked by another context's
	 * frames. Created with IGraphics::CreateRenderContext; IGraphics's own frame methods drive an
	 * implicit primary context.
	 *
	 * Thread affinity, not thread-safety: exactly one thread may drive a given context. Different
	 * contexts may be driven from different threads concurrently.
	 *
	 * A render target presents on the queue that recorded its frames, so a context only accepts
	 * targets it created itself. Scenes must not be shared between contexts.
	 */
	class BGL_API IRenderContext : public core::Ref
	{
	public:
		// Captures a context may have in flight at once (SubmitCapture throws past this).
		static constexpr uint32_t c_MaxPendingCaptures = 2;

		IRenderContext(IRenderContext&&) noexcept      = delete;
		IRenderContext(const IRenderContext&) noexcept = delete;

		IRenderContext&
		operator=(IRenderContext&&) noexcept = delete;

		IRenderContext&
		operator=(const IRenderContext&) noexcept = delete;

		/**
		 * Creates a render output (windowed swapchain or headless offscreen) presented by this
		 * context. One context can own many targets and render to each independently.
		 */
		virtual RenderTargetRef
		CreateRenderTarget(const RenderTargetDesc& desc) = 0;

		/**
		 * Begins a frame bound to `target`; Draw() and EndFrame() act on this target until
		 * EndFrame() returns. Only one frame may be active at a time per context.
		 *
		 * @throws GraphicsError if a frame is already active on this context.
		 */
		virtual void
		BeginFrame(const RenderTargetRef& target) = 0;

		virtual void
		Draw(const RenderJob& job) = 0;

		virtual void
		EndFrame() = 0;

		void
		DrawFrame(const RenderTargetRef& target, const RenderJob& job)
		{
			BeginFrame(target);
			Draw(job);
			EndFrame();
		}

		/**
		 * Recreates a target's backbuffers and depth at the given size, resizing the swapchain too
		 * for a windowed target.
		 *
		 * @throws GraphicsError if called between BeginFrame and EndFrame, or if either dimension
		 *         is zero.
		 */
		virtual void
		Resize(const RenderTargetRef& target, uint32_t width, uint32_t height) = 0;

		virtual void
		ScreenshotPng(const RenderTargetRef& target, const std::string& filepath) = 0;

		/**
		 * Reads `target`'s last presented backbuffer back into a tightly packed RGBA8 image,
		 * blocking until the GPU copy completes -- SubmitCapture + TryResolveCapture in one call.
		 *
		 * @throws GraphicsError if called between BeginFrame and EndFrame.
		 */
		virtual assetlib::ImageData
		ScreenshotToMemory(const RenderTargetRef& target) = 0;

		/**
		 * Records a copy of `target`'s last presented backbuffer into a context-owned readback
		 * buffer and returns without waiting for the GPU. Every ticket must be spent with
		 * TryResolveCapture or DiscardCapture.
		 *
		 * @throws GraphicsError if called between BeginFrame and EndFrame, or if
		 *         c_MaxPendingCaptures captures are already in flight.
		 */
		virtual CaptureTicket
		SubmitCapture(const RenderTargetRef& target) = 0;

		/**
		 * The image of a submitted capture, or nullopt while the GPU copy is still in flight.
		 * Returning an image spends the ticket. May be called between BeginFrame and EndFrame.
		 *
		 * @throws GraphicsError if the ticket is null or already spent.
		 */
		virtual std::optional<assetlib::ImageData>
		TryResolveCapture(CaptureTicket ticket) = 0;

		/**
		 * Abandons a submitted capture without waiting for it; the readback buffer is released
		 * once the GPU is done with it. Spending a ticket twice is a no-op, so teardown paths
		 * need no bookkeeping.
		 */
		virtual void
		DiscardCapture(CaptureTicket ticket) noexcept = 0;

		/**
		 * Per-context equivalent of IGraphics::SetGpuAssertionHandler, with the same contract;
		 * IGraphics's setter reaches only the primary context.
		 */
		virtual void
		SetGpuAssertionHandler(IGpuAssertionHandler* handler) noexcept = 0;

		virtual void
		DiscardPendingGpuAssertions() noexcept = 0;

	protected:
		IRenderContext() noexcept = default;
	};

	using RenderContextRef = core::SharedRef<IRenderContext>;
}

template class BGL_API core::SharedRef<bgl::IRenderContext>;
