#include "dx11/Renderer.h"
#include <Renderer/IRenderer.h>

namespace renderer
{
	std::unique_ptr<IRenderer>
	IRenderer::Create(const RendererOptions& options)
	{
		return std::make_unique<dx::DXRenderer>(options);
	}
}
