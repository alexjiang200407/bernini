// THIS IS A FILE GENERATED FROM AgentReadback.slang. DO NOT EDIT MANUALLY
#pragma once
#include <core/glm.h>

namespace crowd::debug
{
	struct AgentReadback
	{
		glm::vec2 position;
		glm::vec2 facing;
	};

	static_assert(sizeof(AgentReadback) == 16);
	static_assert(offsetof(AgentReadback, position) == 0);
	static_assert(offsetof(AgentReadback, facing) == 8);

}
