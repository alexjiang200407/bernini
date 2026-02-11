#pragma once

namespace gfx
{
	enum class MaterialType : uint16_t
	{
		kInvalid = 0xFFFF,
		kPBR     = 0,
		kCount,
	};

	constexpr std::string_view
	getMatTypeName(MaterialType mat)
	{
		switch (mat)
		{
		case MaterialType::kPBR:
			return "PBR"sv;
		default:
			return "Invalid"sv;
		}
	}
}
