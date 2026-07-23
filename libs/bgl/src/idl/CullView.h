// THIS IS A FILE GENERATED FROM CullView.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	struct CullView
	{
		glm::mat4 viewProj;
		glm::vec4 frustumPlanes[6];
	};

	static_assert(sizeof(CullView) == 160);
	static_assert(offsetof(CullView, viewProj) == 0);
	static_assert(offsetof(CullView, frustumPlanes) == 64);

}
