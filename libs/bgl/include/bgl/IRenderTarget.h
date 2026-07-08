#pragma once
#include <bgl/util.h>
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
		int   width    = 0;
		int   height   = 0;
		bool  headless = false;
		void* wnd      = nullptr;  // HWND for windowed targets; ignored when headless
	};

	/**
	 * A render output: a swapchain (windowed) or offscreen backbuffers (headless),
	 * plus depth, owned independently of the renderer. One Graphics can drive many
	 * RenderTargets. Created with IGraphics::CreateRenderTarget and passed to
	 * IGraphics::BeginFrame / Resize / ScreenshotRaw.
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

	protected:
		IRenderTarget() noexcept = default;
	};

	using RenderTargetHandle = core::SharedRef<IRenderTarget>;
}

template class BGL_API core::SharedRef<bgl::IRenderTarget>;
