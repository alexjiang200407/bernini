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

	uint32_t
	sizeOfElementType(ElementType format);

	nvrhi::Format
	elementTypeToNvrhiFormat(ElementType type);

}
