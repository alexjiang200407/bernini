// THIS IS A FILE GENERATED FROM RenderAgent.slang. DO NOT EDIT MANUALLY
#pragma once
#include <core/glm.h>

namespace crowd
{
	constexpr uint32_t c_RenderSpawned = 0xFFFFFFFFu;

	struct RenderAgent
	{
		glm::vec2 position;
		glm::vec2 facing;
		uint32_t source;
		uint32_t type;
		uint32_t id;
		uint32_t reserved;
	};

	static_assert(sizeof(RenderAgent) == 32);
	static_assert(offsetof(RenderAgent, position) == 0);
	static_assert(offsetof(RenderAgent, facing) == 8);
	static_assert(offsetof(RenderAgent, source) == 16);
	static_assert(offsetof(RenderAgent, type) == 20);
	static_assert(offsetof(RenderAgent, id) == 24);
	static_assert(offsetof(RenderAgent, reserved) == 28);

}
