#pragma once
#include <assetlib_structs/ImageData.h>
#include <bgl/IExternalBuffer.h>
#include <bgl/IGpuAssertionHandler.h>
#include <bgl/IMeshInstanceWriter.h>
#include <bgl/IOverlay.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/SurfaceType.h>
#include <bgl/api.h>
#include <bgl/error.h>
#include <bgl/types/MeshInstanceWriterDesc.h>
#include <bgl/types/PassTiming.h>
#include <bgl/types/RenderJob.h>
#include <bgl/types/SceneDesc.h>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/QueuePoint.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace bgl
{
	class GraphicsError : public ApiError
	{
	public:
		GraphicsError() = delete;
		using ApiError::ApiError;
	};

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

	struct GraphicsOptions
	{
		// Writes the first frame to a .gputrace bundle at this path. Metal only; empty disables it.
		// Needs MTL_CAPTURE_ENABLED=1 in the environment -- Metal refuses to capture without it, and
		// the process must set it before it creates its device, so bgl cannot set it for you.
		std::string gpuCapturePath;

		// Capacities for the graphics-owned resource pools. Unlike SceneDesc's arenas these do not
		// grow, and exhausting one is a hard failure.
		//
		// The names below are the shipped renderer's vocabulary, and each is advisory: a renderer
		// with no equivalent of a given capacity ignores that field rather than failing on it.
		//
		// maxCbvSrvUavs sizes the shader-visible heap -- how many *descriptors* exist. maxBuffers,
		// maxSrvs and maxBufferSrvs size the resource pools that draw from it, and must together fit
		// inside it alongside the unbound sentinel index 0 is reserved for.
		uint32_t maxCbvSrvUavs = 1065;
		uint32_t maxBuffers    = 500;
		uint32_t maxSrvs       = 500;

		// Second, structured views of buffers. Only an arena whose records hold resource handles
		// needs one, so far fewer than there are buffers.
		uint32_t maxBufferSrvs = 64;

		// Sized together, because one render target draws on both: seven RTVs -- two swapchain
		// images, two TAA history buffers, and one each for motion vectors, scene colour and the
		// outline mask -- plus up to eleven more once the target blooms (six downsample levels and
		// five upsample ones) -- against the single DSV of its depth buffer. So these carry eight
		// targets with bloom on all of them, alongside the one RTV the BRDF LUT holds for the life
		// of the device. A viewport, a material preview, a texture preview and the thumbnail cache
		// are already four.
		uint32_t maxRtvs            = 160;
		uint32_t maxDsvs            = 8;
		uint32_t maxTextures        = 1000;
		uint32_t maxSamplers        = 128;
		uint32_t maxReadbackBuffers = 64;
	};

	/**
	 * The device and its one submission context: creates every GPU resource, and records and
	 * presents every frame.
	 *
	 * Thread affinity, not thread-safety: exactly one thread may drive the frame methods below.
	 */
	class BGL_API IGraphics : public core::Ref
	{
	public:
		// Captures that may be in flight at once (SubmitCapture throws past this).
		static constexpr uint32_t c_MaxPendingCaptures = 2;

		IGraphics(IGraphics&&) noexcept      = delete;
		IGraphics(const IGraphics&) noexcept = delete;

		IGraphics&
		operator=(IGraphics&&) noexcept = delete;

		IGraphics&
		operator=(const IGraphics&) noexcept = delete;

		/**
		 * Creates a render output (windowed swapchain or headless offscreen). One
		 * Graphics can own many targets and render to each independently.
		 */
		virtual RenderTargetRef
		CreateRenderTarget(const RenderTargetDesc& desc) = 0;

		/**
		 * Begins a frame bound to `target`; Draw() and EndFrame() act on this target
		 * until EndFrame() returns. Only one frame may be active at a time.
		 *
		 * @throws GraphicsError if a frame is already active.
		 */
		virtual void
		BeginFrame(const RenderTargetRef& target) = 0;

		virtual void
		Draw(const RenderJob& job) = 0;

		/**
		 * Queues 2D draws to land on this frame's output after post-processing, in submission
		 * order across every call this frame. A frame with no call draws no overlay pass at all.
		 *
		 * @throws GraphicsError if called outside BeginFrame/EndFrame, if `job.overlay` is null, or
		 *         if a draw names a geometry or texture that is null or no longer live.
		 */
		virtual void
		DrawOverlay(const OverlayJob& job) = 0;

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
		 * Recreates a target's backbuffers and depth at the given size, resizing the
		 * swapchain too for a windowed target.
		 *
		 * @throws GraphicsError if called between BeginFrame and EndFrame, or if either
		 *         dimension is zero.
		 */
		virtual void
		Resize(const RenderTargetRef& target, uint32_t width, uint32_t height) = 0;

		/**
		 * Re-derives a target's render size from its output size and recreates the attachments the
		 * geometry passes draw into. The presented size is unchanged, and so is every capture; what
		 * moves is how densely the frame is sampled before the TAA resolve reconstructs it.
		 *
		 * The accumulation is discarded, since a history gathered on one render grid describes
		 * samples the new one does not take.
		 *
		 * @throws GraphicsError if called between BeginFrame and EndFrame, or if `scale` is not a
		 *         positive, finite number.
		 */
		virtual void
		SetRenderScale(const RenderTargetRef& target, float scale) = 0;

		/**
		 * Blocks until every submitted frame -- queued presents included -- has drained from the
		 * GPU. A client that is about to hide the last window it presents to should call this while
		 * the window is still on screen: a present left pending across the hide is never consumed,
		 * and every later fence wait on the queue sits behind it.
		 *
		 * Must not be called between BeginFrame and EndFrame.
		 */
		virtual void
		WaitIdle() noexcept = 0;

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
		 * Records a copy of `target`'s last presented backbuffer into a readback buffer and returns
		 * without waiting for the GPU. Every ticket must be spent with TryResolveCapture or
		 * DiscardCapture.
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
		 * What each pass of the last completed timed frame on `target` cost on the GPU, in
		 * execution order, under the id of the frame it measured -- the rows behind an on-screen
		 * breakdown. A frame's rows arrive once its fence has passed, so they trail the frame that
		 * wrote them by one or two, and a caller reading every frame tells one sample from the next
		 * by PassTimings::frame. No rows while IRenderTarget::SetGpuTimingEnabled is off, before the
		 * first timed frame completes, and on a device that cannot sample a timestamp at a pass
		 * boundary. May be called mid-frame.
		 */
		[[nodiscard]] virtual PassTimings
		GetPassTimings(const RenderTargetRef& target) = 0;

		/**
		 * The surfaces registered from the GPU context's `clientShaderDir`, in slot order, each
		 * carrying the `MaterialType` its materials are created with.
		 *
		 * Fixed at construction: every pipeline that can draw one is built there, so a surface added
		 * to the directory afterwards is seen at the next launch. Empty when no directory was named.
		 */
		[[nodiscard]] virtual std::span<const SurfaceType>
		GetSurfaceTypes() const noexcept = 0;

		virtual SceneRef
		CreateScene(SceneDesc desc) = 0;

		virtual SceneViewRef
		CreateSceneView(const SceneRef& scene, uint32_t initialInstances) = 0;

		/**
		 * An overlay's handles are usable on any target this graphics draws; several overlays may
		 * be drawn in one frame.
		 */
		virtual OverlayRef
		CreateOverlay() = 0;

		/**
		 * Compiles a caller's kernel for instance blocks: `desc.slangTypeName` in `desc.slangModuleName` must conform
		 * to IMeshInstanceWriter in `bgl.MeshInstanceWriter`. Compiled once, here; a writer used by many
		 * blocks and views is not compiled again.
		 *
		 * @throws GraphicsError if called between BeginFrame and EndFrame, if either name is empty,
		 *         or if the module does not compile against the contract -- the compiler's
		 *         diagnostics are in the message.
		 */
		virtual MeshInstanceWriterRef
		CreateMeshInstanceWriter(const MeshInstanceWriterDesc& desc) = 0;

		/**
		 * Adopts another owner's buffer, read-only, so an instance writer's parameters can bind it.
		 * The renderer orders nothing against the exporter: a frame that reads the buffer waits for
		 * the exporter's writes with WaitBeforeNextFrame, and the exporter waits for the frames that
		 * read it (GetLastFrameDone) before it writes the memory again.
		 *
		 * @throws GraphicsError if `desc` is null, has a zero stride or element count, asks for a
		 *         writable buffer, or names an object this backend cannot adopt.
		 */
		virtual ExternalBufferRef
		ImportBuffer(const bgpu::NativeBufferDesc& desc) = 0;

		/**
		 * Makes the next frame's GPU work wait until `point` has passed on its queue. The CPU does
		 * not wait. Every point given before a BeginFrame applies to that frame alone.
		 *
		 * @throws GraphicsError if called between BeginFrame and EndFrame -- a frame's command list
		 *         is already open by then, and a wait cannot reach it -- or if `point` is null.
		 */
		virtual void
		WaitBeforeNextFrame(const bgpu::QueuePoint& point) = 0;

		/**
		 * The point on the renderer's queue past which every frame submitted so far has finished:
		 * what an owner whose memory those frames read waits on before writing it again. Null
		 * before the first frame.
		 *
		 * @throws GraphicsError if called between BeginFrame and EndFrame, when the open frame is
		 *         not yet submitted and the point would not cover it.
		 */
		[[nodiscard]] virtual bgpu::QueuePoint
		GetLastFrameDone() const = 0;

		/**
		 * Registers a sink for GPU assertions (dbg_raise) the engine detects during
		 * BeginFrame. While a handler is set it replaces the default behavior of
		 * crashing when an assertion fires; nullptr (the default) restores the crash.
		 * GPU assertions are a Debug-build facility (BERNINI_GPU_DEBUG); in Release the
		 * handler is never invoked.
		 *
		 * This setter performs NO GPU/frame synchronization: it only swaps a non-owning
		 * CPU pointer (no fence wait, no command recording) and takes effect at the next
		 * BeginFrame's inspection. It is not thread-safe -- call it on the render thread,
		 * like BeginFrame/Draw/EndFrame (calling it mid-frame is fine; it only affects
		 * the next frame's inspection).
		 *
		 * Lifetime note tied to frame latency: assertions are read back and reported a
		 * few frames AFTER they fire (the readback ring is c_SwapchainImageCount deep), so the
		 * handler object must stay valid across that window -- simplest rule: it must
		 * outlive this IGraphics. Clearing to nullptr does not cancel an already
		 * in-flight assertion; that pending report then falls back to the crash path --
		 * call DiscardPendingGpuAssertions() first to drop it.
		 */
		virtual void
		SetGpuAssertionHandler(IGpuAssertionHandler* handler) noexcept = 0;

		/**
		 * Drops any GPU assertions that have fired but are still in flight in the
		 * readback ring (the frame-latency window described above) WITHOUT invoking the
		 * handler or crashing. Call it when you intentionally want to abandon pending
		 * assertions -- e.g. before clearing the handler, or before tearing down a
		 * handler that would otherwise outlive nothing -- so they do not fall back to
		 * the crash path at the next inspection (BeginFrame or destruction).
		 *
		 * Like SetGpuAssertionHandler this performs NO GPU/frame synchronization: it
		 * only resets CPU-side pending state and is not thread-safe (call it on the
		 * render thread). No-op in Release / without BERNINI_GPU_DEBUG.
		 */
		virtual void
		DiscardPendingGpuAssertions() noexcept = 0;

	protected:
		IGraphics() noexcept = default;
	};

	using GraphicsRef = core::SharedRef<IGraphics>;

	/**
	 * The renderer on a device the application created and may share with other owners: the
	 * renderer compiles through the context's Slang sessions and draws on its device, and holds the
	 * context for as long as it lives. `opts` is the renderer's own -- its pools and its shader
	 * cache; the device-level choices are the context's.
	 *
	 * @throws ApiError for a client shader directory that does not exist or holds an invalid surface.
	 */
	BGL_API GraphicsRef
	CreateGraphics(core::SharedRef<bgpu::GpuContext> context, const GraphicsOptions& opts);
}

template class BGL_API core::SharedRef<bgl::IGraphics>;
