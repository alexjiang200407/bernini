#include "dx11/DXError.h"
#include <Renderer/Renderer.h>

namespace renderer
{
	RendererException::RendererException(
		const std::string&   cause,
		std::source_location loc) noexcept : cause{ cause }, loc(loc)
	{
		message = std::format(
			"  Cause:   {}\n"
			"  File:    {}\n"
			"  Func:    {}\n"
			"  Line:    {}",
			cause,
			loc.file_name(),
			loc.function_name(),
			loc.line());
	}
}
