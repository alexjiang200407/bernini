#pragma once
#include <Renderer/RendererException.h>

namespace renderer
{
	struct RendererOptions
	{
		int width  = 0;
		int height = 0;
	};

	class IRenderer
	{
	public:
		virtual ~IRenderer() = default;

		virtual void
		DrawFrame() = 0;

		[[nodiscard]]
		static std::unique_ptr<IRenderer>
		Create(const RendererOptions& options = {});
	};

}
