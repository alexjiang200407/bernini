#include "buffer/types/ElementType.h"

namespace gfx
{
	uint32_t
	sizeOfElementType(ElementType type)
	{
		switch (type)
		{
		case ElementType::kEmpty:
			return 0;
		case ElementType::kFloat:
			return 4;
		case ElementType::kFloat2:
			return 8;
		case ElementType::kFloat3:
			return 12;
		case ElementType::kFloat4:
			return 16;
		case ElementType::kFloat4x4:
			return 64;
		case ElementType::kShort:
		case ElementType::kUShort:
			return 2;
		case ElementType::kBool:
		case ElementType::kInt:
		case ElementType::kUInt:
			return 4;
		}

		return 0;
	}

	nvrhi::Format
	elementTypeToNvrhiFormat(ElementType type)
	{
		switch (type)
		{
		case ElementType::kEmpty:
			return nvrhi::Format::UNKNOWN;
		case ElementType::kFloat:
			return nvrhi::Format::R32_FLOAT;
		case ElementType::kFloat2:
			return nvrhi::Format::RG32_FLOAT;
		case ElementType::kFloat3:
			return nvrhi::Format::RGB32_FLOAT;
		case ElementType::kFloat4:
			return nvrhi::Format::RGBA32_FLOAT;
		case ElementType::kFloat4x4:
			return nvrhi::Format::RGBA32_FLOAT;
		case ElementType::kShort:
			return nvrhi::Format::R16_SINT;
		case ElementType::kUShort:
			return nvrhi::Format::R16_UINT;
		case ElementType::kInt:
			return nvrhi::Format::R32_SINT;
		case ElementType::kUInt:
			return nvrhi::Format::R32_UINT;
		case ElementType::kBool:
			return nvrhi::Format::R32_UINT;
		}

		return nvrhi::Format::UNKNOWN;
	}
}
