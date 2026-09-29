// THIS IS A FILE GENERATED FROM OverlayVertex.slang. DO NOT EDIT MANUALLY
#pragma once
#include <core/glm.h>

namespace bgl
{
	struct OverlayVertex
	{
		glm::vec2 position;
		glm::vec2 uv;
		uint32_t color;
		uint32_t reserved;
	};

	static_assert(sizeof(OverlayVertex) == 24);
	static_assert(offsetof(OverlayVertex, position) == 0);
	static_assert(offsetof(OverlayVertex, uv) == 8);
	static_assert(offsetof(OverlayVertex, color) == 16);
	static_assert(offsetof(OverlayVertex, reserved) == 20);

}
