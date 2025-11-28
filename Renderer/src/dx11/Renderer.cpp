#include "dx11/DXRenderer.h"
#include <Renderer/Renderer.h>

namespace renderer
{
	std::unique_ptr<IRenderer>
	IRenderer::Create(const RendererOptions& options)
	{
		return std::make_unique<dx::DXRenderer>(options);
	}
}
