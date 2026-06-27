// THIS IS A FILE GENERATED FROM Vertex.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	struct Vertex
	{
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec4 tangent;
	};

	static_assert(sizeof(Vertex) == 48);
	static_assert(offsetof(Vertex, pos) == 0);
	static_assert(offsetof(Vertex, normal) == 12);
	static_assert(offsetof(Vertex, uv) == 24);
	static_assert(offsetof(Vertex, tangent) == 32);

}
