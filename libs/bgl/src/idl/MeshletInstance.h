// THIS IS A FILE GENERATED FROM MeshletInstance.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl::idl
{
	struct MeshletInstance
	{
		uint32_t instanceIndex;
		uint32_t meshletIndex;
	};

	static_assert(sizeof(MeshletInstance) == 8);
	static_assert(offsetof(MeshletInstance, instanceIndex) == 0);
	static_assert(offsetof(MeshletInstance, meshletIndex) == 4);

}
