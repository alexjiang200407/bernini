#pragma once
#include <Renderer/Renderer.h>

namespace renderer::dx
{
	class DXRenderer : public IRenderer
	{
	public:
		explicit DXRenderer(const struct RendererOptions& opts);

		void
		StartFrame() override;
	};
}
