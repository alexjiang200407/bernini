#pragma once
#include <bgl/IGpuAssertionHandler.h>
#include <bgl/IRenderContext.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/RenderJob.h>
#include <bgl/api.h>
#include <bgl/error.h>
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

		bool enableDebugLayer         = false;
		bool enableGPUValidationLayer = false;
		bool enablePixDebug           = false;
		bool strictError              = false;

		LogLevel logLevel = LogLevel::kError;

		// Directory for the persistent shader cache. Empty disables caching.
		std::string shaderCacheDir;

		// Capacities for the graphics-owned descriptor heaps / resource pools.
		uint32_t maxCbvSrvUavs      = 1000;
		uint32_t maxRtvs            = 5;
		uint32_t maxDsvs            = 5;
		uint32_t maxTextures        = 1000;
		uint32_t maxSamplers        = 128;
		uint32_t maxReadbackBuffers = 64;
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
		 * Creates a submission context over the device: its own queue, recorder and frame state, so
		 * its frames neither block nor are blocked by another context's. Every render target, frame
		 * and screenshot goes through a context -- there is no implicit primary. The device supports
		 * a small fixed number of contexts (currently 8); exceeding it asserts. See IRenderContext
		 * for the threading and target/scene rules.
		 */
		virtual RenderContextRef
		CreateRenderContext() = 0;

		virtual SceneRef
		CreateScene(SceneDesc desc) = 0;

		virtual SceneViewRef
		CreateSceneView(const SceneRef& scene, uint32_t maxInstances) = 0;

	protected:
		IGraphics() noexcept = default;
	};

	using GraphicsRef = core::SharedRef<IGraphics>;

	BGL_API GraphicsRef
	CreateGraphics(const GraphicsOptions& opts);
}

template class BGL_API core::SharedRef<bgl::IGraphics>;
