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
		kCount = 4,
	};

	static_assert(sizeof(MaterialType) == 4);

}
