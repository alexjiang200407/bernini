// THIS IS A FILE GENERATED FROM MaterialType.slang. DO NOT EDIT MANUALLY
#pragma once

namespace bgl
{
	enum class MaterialType : uint32_t
	{
		kInvalid = uint32_t(-1),
		kNull = 0,
		kAssert = 1,
		kPBR = 2,
		kLoosePbr = 3,
		kGame0 = 4,
		kGame1 = 5,
		kGame2 = 6,
		kGame3 = 7,
		kCount = 8,
	};

	static_assert(sizeof(MaterialType) == 4);

}
