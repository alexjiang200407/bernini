#pragma once
#include <bgl/api.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	/**
	 * Describes a render output. A windowed target presents to `wnd`'s swapchain; a
	 * headless target renders to offscreen backbuffers (used by tests / asset cooking).
	 */
	struct RenderTargetDesc
	{
		int  width      = 0;
		int  height     = 0;
		bool headless   = false;
		bool taaEnabled = false;

		// The native surface a windowed target presents into: an HWND on D3D12, a CAMetalLayer
		// on Metal. Ignored when headless.
		void* wnd = nullptr;
	};

	/**
	 * A render output: a swapchain (windowed) or offscreen backbuffers (headless),
	 * plus depth, owned independently of the renderer. One Graphics can drive many
	 * RenderTargets. Created with IGraphics::CreateRenderTarget and passed to
	 * IGraphics::BeginFrame / Resize / ScreenshotPng.
	 */
	class BGL_API IRenderTarget : public core::Ref
	{
	public:
		IRenderTarget(IRenderTarget&&) noexcept      = delete;
		IRenderTarget(const IRenderTarget&) noexcept = delete;

		IRenderTarget&
		operator=(IRenderTarget&&) noexcept = delete;

		IRenderTarget&
		operator=(const IRenderTarget&) noexcept = delete;

		virtual uint32_t
		GetWidth() const noexcept = 0;

		virtual uint32_t
		GetHeight() const noexcept = 0;

		/** Whether temporal AA is running on this target -- jitter applied and history accumulated. */
		[[nodiscard]] virtual bool
		IsTaaEnabled() const noexcept = 0;

		/**
		 * Turns temporal AA on or off for subsequent frames, so it can be compared against itself
		 * without recreating the target. Turning it off discards the accumulation rather than
		 * pausing it: the frames it would have to bridge are not rendered, so the first frame after
		 * turning it back on starts from the scene colour.
		 *
		 * @throws GraphicsError if `enabled` and the target was created without
		 *         RenderTargetDesc::taaEnabled -- it has no history to accumulate into.
		 */
		virtual void
		SetTaaEnabled(bool enabled) = 0;

	protected:
		IRenderTarget() noexcept = default;
	};

	using RenderTargetRef = core::SharedRef<IRenderTarget>;
}

template class BGL_API core::SharedRef<bgl::IRenderTarget>;
