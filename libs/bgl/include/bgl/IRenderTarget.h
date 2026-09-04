#pragma once
#include <bgl/api.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>
#include <cstdint>

namespace bgl
{
	/**
	 * Describes a render output. A windowed target presents to `wnd`'s swapchain; a
	 * headless target renders to offscreen backbuffers (used by tests / asset cooking).
	 */
	struct RenderTargetDesc
	{
		// The output size: what is presented, captured, and accumulated into.
		int  width      = 0;
		int  height     = 0;
		bool headless   = false;
		bool taaEnabled = false;

		// How dense the grid the geometry passes render on is, relative to the output size. Below
		// 1.0 the TAA resolve reconstructs the output from the jittered render frames; above it,
		// the same resolve is a downsample. Nothing outside the resolve sees both grids.
		float renderScale = 1.0f;

		// The width, in output pixels, of the kernel the TAA resolve reconstructs each output pixel
		// with. Narrower is sharper and slower to settle; see IRenderTarget::SetTaaReconstructionWidth.
		float taaReconstructionWidth = 0.4f;

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

		/** The output size: the backbuffer's, the TAA history's, and every capture's. */
		virtual uint32_t
		GetWidth() const noexcept = 0;

		virtual uint32_t
		GetHeight() const noexcept = 0;

		/**
		 * The size the geometry passes render at -- the output size scaled by
		 * `RenderTargetDesc::renderScale` and floored at one pixel. Equal to the output size at
		 * scale 1.0.
		 */
		[[nodiscard]] virtual uint32_t
		GetRenderWidth() const noexcept = 0;

		[[nodiscard]] virtual uint32_t
		GetRenderHeight() const noexcept = 0;

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

		/** The width, in output pixels, of the TAA resolve's reconstruction kernel. */
		[[nodiscard]] virtual float
		GetTaaReconstructionWidth() const noexcept = 0;

		/**
		 * Sets how wide a kernel each output pixel weights the render sample nearest it by, in
		 * output pixels, from the next frame on. Nothing is reallocated and the accumulation is
		 * kept: this is a per-frame shader constant, so it can be swept while watching one scene.
		 *
		 * Narrower sharpens a held frame without limit, because a still pixel eventually sees every
		 * jitter phase. What it costs is the frames a moving pixel waits for the phase that serves
		 * it, and only a moving image shows that -- which is why this is a knob and not a constant.
		 *
		 * It cannot change an image where the render and output grids coincide, whatever it is set
		 * to: there is one phase there, and it weighs itself against its own mean.
		 *
		 * @throws GraphicsError if `width` is not a positive, finite number.
		 */
		virtual void
		SetTaaReconstructionWidth(float width) = 0;

		/** Whether the selection outline is drawn on this target. On by default. */
		[[nodiscard]] virtual bool
		IsOutlineEnabled() const noexcept = 0;

		/**
		 * Turns the selection outline on or off for subsequent frames. The views' selection marks
		 * are untouched -- the toggle is presentation, not state -- so re-enabling shows the
		 * current selection again.
		 */
		virtual void
		SetOutlineEnabled(bool enabled) noexcept = 0;

	protected:
		IRenderTarget() noexcept = default;
	};

	using RenderTargetRef = core::SharedRef<IRenderTarget>;
}

template class BGL_API core::SharedRef<bgl::IRenderTarget>;
