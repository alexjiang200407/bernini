#pragma once
#include <Renderer/RendererException.h>

namespace renderer
{
	struct RendererOptions
	{
		int width  = 0;
		int height = 0;
	};

	class Renderer
	{
	public:
		Renderer(const RendererOptions& opts = {});

		void
		DrawFrame();
	};

}
