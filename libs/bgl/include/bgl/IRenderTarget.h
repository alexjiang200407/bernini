#pragma once
#include <bgl/api.h>
#include <bgl/types/PostProcess.h>
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

		// How hard an upscaled image is sharpened before the display curve, in [0, 1]. Zero skips
		// the sharpen, and so does a render scale of 1 or more; see IRenderTarget::SetTaaSharpness.
		float taaSharpness = 1.0f;

		// What the target's frames end in; see IRenderTarget::SetPostProcess.
		PostProcess postProcess;

		// The native surface a windowed target presents into: an HWND on D3D12 and Vulkan, a
		// CAMetalLayer on Metal. Ignored when headless. On Vulkan the target takes on its window's
		// size when a present finds it resized (docs/bgl_api.md). The Metal layer and its window are the caller's: the
		// backbuffer is sRGB-encoded, and the window's colour space must be set to sRGB explicitly
		// or the layer is composited unmatched (docs/known_issues.md).
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

		/** How hard an upscaled image is sharpened, in [0, 1]; zero is off. */
		[[nodiscard]] virtual float
		GetTaaSharpness() const noexcept = 0;

		/**
		 * Sets the contrast-adaptive sharpen applied to the resolved image from the next frame on.
		 * Nothing is reallocated and the accumulation is kept, since the sharpen reads the history
		 * and never writes it. Zero skips it; above zero it follows FSR 2's mapping, a strength
		 * of exp2(2s - 2). It runs only on frames the TAA resolve ran, and only below a render
		 * scale of 1: at native or above there is no upscale's softness to put back, and a sharpen
		 * there only pushes past the native image.
		 *
		 * @throws GraphicsError if `sharpness` is not a finite number in [0, 1].
		 */
		virtual void
		SetTaaSharpness(float sharpness) = 0;

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

		[[nodiscard]] virtual PostProcess
		GetPostProcess() const = 0;

		/**
		 * What subsequent frames end in: AgX, and the effects around it. A target's, not a view's:
		 * it runs once on the output, however many views draw into it. Turning bloom on allocates
		 * its chain at the first frame that blooms and keeps it when it is turned off (~11 MiB at
		 * 1080p, ~44 MiB at 4K); nothing else allocates.
		 *
		 * @throws GraphicsError naming the first field outside its documented range, and keeps the
		 *         post-process the target had: BloomSettings' intensity or threshold negative or not
		 *         finite, softKnee or scatter outside [0, 1]; a ColorGradeSettings temperature or
		 *         tint outside [-100, 100], a slope, saturation or contrast negative or not finite,
		 *         an offset outside [-1, 1], a power not positive and finite, a vignette intensity
		 *         outside [0, 1] or smoothness outside (0, 1]; a FilmGrainSettings intensity outside
		 *         [0, 1] or a size not positive and finite; a ColorSplitSettings offset or radial not
		 *         finite.
		 */
		virtual void
		SetPostProcess(const PostProcess& postProcess) = 0;

		/** Whether every pass of a frame drawn to this target is timed on the GPU. Off by default. */
		[[nodiscard]] virtual bool
		IsGpuTimingEnabled() const noexcept = 0;

		/**
		 * Turns per-pass GPU timing on or off from the next frame on; IGraphics::GetPassTimings
		 * reads the result. Nothing is allocated either way -- the slots exist from creation -- but
		 * a timed frame costs a resolve and, on Metal, an encoder ended at every pass boundary, so
		 * it is off until something reads the rows. Turning it off drops the rows already read.
		 */
		virtual void
		SetGpuTimingEnabled(bool enabled) noexcept = 0;

	protected:
		IRenderTarget() noexcept = default;
	};

	using RenderTargetRef = core::SharedRef<IRenderTarget>;
}

template class BGL_API core::SharedRef<bgl::IRenderTarget>;
