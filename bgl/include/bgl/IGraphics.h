#pragma once
#include <bgl/IScene.h>
#include <bgl/RenderContext.h>
#include <bgl/util.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
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

		int      width;
		int      height;
		bool     headless;
		bool     enableDebugLayer;
		bool     enableGPUValidationLayer;
		bool     enablePixDebug;
		void*    wnd;
		LogLevel logLevel = LogLevel::kError;
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

		virtual void
		BeginFrame() = 0;

		virtual void
		Draw(const RenderContext& context) = 0;

		virtual void
		EndFrame() = 0;

		void
		DrawFrame(const RenderContext& context)
		{
			BeginFrame();
			Draw(context);
			EndFrame();
		}

		virtual void
		ScreenshotRaw(const std::string& filepath) = 0;

		virtual SceneHandle
		CreateScene(SceneDesc desc) = 0;

	protected:
		IGraphics() noexcept = default;
	};

	using GraphicsHandle = core::SharedRef<IGraphics>;

	template class BGL_API core::SharedRef<IGraphics>;

	BGL_API GraphicsHandle
	CreateGraphics(const GraphicsOptions& opts);
}
