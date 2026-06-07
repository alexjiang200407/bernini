#include <bgl/Graphics.h>

namespace bgl
{
	IGraphics::~IGraphics() noexcept { logger::info("Destroying Graphics"); }
}
