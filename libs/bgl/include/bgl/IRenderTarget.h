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
		// on Metal. Ignored when headless. The Metal layer and its window are the caller's: the
		// backbuffer is sRGB-encoded, and the window's colour space must be set to sRGB explicitly
		// or the layer is composited unmatched (docs/known_issues.md).
		void* wnd = nullptr;
	};

	/**
	 * How a target blooms: what part of the linear HDR scene spills into a glow, and how strongly.
	 * Applied ahead of the display curve, so the glow is of the scene's radiance rather than of the
	 * displayed image. Every field is a per-frame shader constant -- nothing is reallocated by a
	 * change.
	 */
	struct BloomSettings
	{
		// The glow's weight in the combine: sceneColor + intensity * bloom. Zero adds nothing but
		// still pays for the chain; turn bloom off instead.
		float intensity = 0.25f;

		// The linear radiance where a pixel starts to contribute, after exposure. Exposure puts a
		// scene's average near middle grey (0.18), so 0.5 blooms bright surfaces and highlights and
		// leaves mid-tones alone; 1.0 leaves almost nothing but specular peaks, and 0.0 blooms
		// everything, which reads as soft focus.
		float threshold = 0.5f;

		// How gradually the threshold takes hold, as a share of it: 0 is a hard cut, 1 fades in
		// from half the threshold.
		float softKnee = 0.5f;

		// How far the glow spreads: the weight of the coarser level folded in at each upsample.
		// Low keeps a tight halo; 1.0 lets the widest level through undiminished.
		float scatter = 0.7f;
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

		/** Whether bloom runs on this target. Off by default. */
		[[nodiscard]] virtual bool
		IsBloomEnabled() const noexcept = 0;

		/**
		 * Turns bloom on or off for subsequent frames. Unlike TAA there is nothing to opt into at
		 * creation: the chain it renders through is allocated at the first frame that needs it, so
		 * enabling cannot fail here. Turning it off keeps the chain -- bloom holds no history, so
		 * there is nothing stale to discard and re-enabling costs nothing. What is kept is about a
		 * third of the output size in RGBA16 twice over, roughly 11 MiB per 1080p target and 44 MiB
		 * at 4K, charged to the device-texture memory tag.
		 */
		virtual void
		SetBloomEnabled(bool enabled) noexcept = 0;

		[[nodiscard]] virtual BloomSettings
		GetBloomSettings() const noexcept = 0;

		/**
		 * Sets how the target blooms from the next frame on. Per-frame shader constants only --
		 * nothing is reallocated, so the settings can be swept while watching one scene.
		 *
		 * @throws GraphicsError if `intensity` or `threshold` is negative or not finite, or
		 *         `softKnee` or `scatter` is outside [0, 1].
		 */
		virtual void
		SetBloomSettings(const BloomSettings& settings) = 0;

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
