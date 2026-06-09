#pragma once
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
		virtual void
		DrawFrame() = 0;
	};

	using GraphicsHandle = core::SharedRef<IGraphics>;

	template class BGL_API core::SharedRef<IGraphics>;

	BGL_API GraphicsHandle
	CreateGraphics(const GraphicsOptions& opts);
}
