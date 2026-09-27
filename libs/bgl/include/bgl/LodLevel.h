// THIS IS A FILE GENERATED FROM LodLevel.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl
{
	enum class LodLevel : uint32_t
	{
		kLod0 = 0,
		kLod1 = 1,
		kLod2 = 2,
		kLod3 = 3,
		kLod4 = 4,
		kLod5 = 5,
		kLod6 = 6,
		kLod7 = 7,
		kCount = 8,
	};

	static_assert(sizeof(LodLevel) == 4);

	constexpr uint32_t cMaxMeshLods = uint32_t(LodLevel::kCount);

}
