#pragma once
#include <bgl/IScene.h>
#include <bgl/util.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	struct GraphicsOptions
	{
		int   width;
		int   height;
		bool  headless;
		bool  enableDebugLayer;
		bool  enableGPUValidationLayer;
		bool  enablePixDebug;
		void* wnd;
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
		DrawFrame(IScene* scene) = 0;

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
