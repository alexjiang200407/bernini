#include <assetlib_structs/VertexLayout.h>
#include <cstdint>
#include <optional>

namespace assetlib
{
	uint32_t
	formatSize(VertexFormat format) noexcept
	{
		switch (format)
		{
		case VertexFormat::kFloat32x2:
			return 8;
		case VertexFormat::kFloat32x3:
			return 12;
		case VertexFormat::kFloat32x4:
			return 16;
		case VertexFormat::kUnorm8x4:
			return 4;
		case VertexFormat::kUnorm16x2:
			return 4;
		case VertexFormat::kUnorm16x4:
			return 8;
		case VertexFormat::kUint16x4:
			return 8;
		}
		return 0;
	}

	const VertexAttribute*
	findAttribute(const VertexLayout& layout, VertexSemantic semantic) noexcept
	{
		for (uint8_t i = 0; i < layout.attributeCount; ++i)
		{
			if (layout.attributes[i].semantic == semantic)
				return &layout.attributes[i];
		}
		return nullptr;
	}

	std::optional<uint16_t>
	attributeOffset(const VertexLayout& layout, VertexSemantic semantic) noexcept
	{
		for (uint32_t i = 0; i < layout.attributeCount; ++i)
			if (layout.attributes[i].semantic == semantic)
				return layout.attributes[i].offset;

		return std::nullopt;
	}
}
