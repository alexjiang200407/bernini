#pragma once
#include <bgl/IGpuAssertionHandler.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/RenderContext.h>
#include <bgl/error.h>
#include <bgl/util.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class GraphicsError : public ApiError
	{
	public:
		GraphicsError() = delete;
		using ApiError::ApiError;
	};

	struct GraphicsOptions
	{
		enum class LogLevel
		{
			kTrace = 0,
			kDebug,
			kInfo,
			kWarn,
			kError,
			kCritical,
			kOff,
		};

		bool enableDebugLayer;
		bool enableGPUValidationLayer;
		bool enablePixDebug;
		bool strictError = false;

		LogLevel logLevel = LogLevel::kError;

		// Capacities for the graphics-owned descriptor heaps / resource pools.
		uint32_t maxCbvSrvUavs = 1000;
		uint32_t maxRtvs       = 5;
		uint32_t maxDsvs       = 5;
		uint32_t maxTextures   = 1000;
	};

	class BGL_API IGraphics : public core::Ref
	{
	public:
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
		virtual RenderTargetHandle
		CreateRenderTarget(const RenderTargetDesc& desc) = 0;

		/**
		 * Begins a frame bound to `target`; Draw() and EndFrame() act on this target
		 * until EndFrame() returns. Only one frame may be active at a time.
		 *
		 * @throws GraphicsError if a frame is already active.
		 */
		virtual void
		BeginFrame(const RenderTargetHandle& target) = 0;

		virtual void
		Draw(const RenderContext& context) = 0;

		virtual void
		EndFrame() = 0;

		void
		DrawFrame(const RenderTargetHandle& target, const RenderContext& context)
		{
			BeginFrame(target);
			Draw(context);
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
		Resize(const RenderTargetHandle& target, uint32_t width, uint32_t height) = 0;

		virtual void
		ScreenshotRaw(const RenderTargetHandle& target, const std::string& filepath) = 0;

		virtual SceneHandle
		CreateScene(SceneDesc desc) = 0;

		virtual SceneViewHandle
		CreateSceneView(const SceneHandle& scene, uint32_t maxInstances) = 0;

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
		 * few frames AFTER they fire (the readback ring is c_BufferCount deep), so the
		 * handler object must stay valid across that window -- simplest rule: it must
		 * outlive this IGraphics. Clearing to nullptr does not cancel an already
		 * in-flight assertion; that pending report then falls back to the crash path.
		 */
		virtual void
		SetGpuAssertionHandler(IGpuAssertionHandler* handler) noexcept = 0;

	protected:
		IGraphics() noexcept = default;
	};

	using GraphicsHandle = core::SharedRef<IGraphics>;

	template class BGL_API core::SharedRef<IGraphics>;

	BGL_API GraphicsHandle
	CreateGraphics(const GraphicsOptions& opts);
}
