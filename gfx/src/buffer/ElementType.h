#pragma once

namespace gfx
{
	enum class ElementType
	{
		kInvalid = -1,
		kEmpty,

		kFloat,
		kFloat2,
		kFloat3,
		kFloat4,
		kFloat4x4,

		kShort,
		kUShort,
		kInt,
		kUInt,

		kBool,
	};
}
