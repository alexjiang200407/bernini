#pragma once

namespace gfx
{
	struct MeshletDispatchArg
	{
		uint32_t threadGroupCountX;
		uint32_t threadGroupCountY;
		uint32_t threadGroupCountZ;
	};
}
