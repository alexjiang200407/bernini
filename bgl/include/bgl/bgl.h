#pragma once

#include <core/pimpl/PImpl.h>

#ifdef BGL_EXPORTS
#	define BGL_API __declspec(dllexport)
#else
#	define BGL_API __declspec(dllimport)
#endif

namespace bgl
{
	class GraphicsImpl;
}

template class BGL_API core::PImpl<bgl::GraphicsImpl>;

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

	class BGL_API Graphics : public core::PImpl<GraphicsImpl>
	{
	public:
		Graphics(const GraphicsOptions& opts);
		~Graphics() noexcept;

		void
		DrawFrame() const;
	};
}
