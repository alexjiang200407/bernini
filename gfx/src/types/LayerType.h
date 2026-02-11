#pragma once

namespace gfx
{
	enum class LayerType : uint8_t
	{
		kInvalid     = 0xFF,
		kBackground  = 0,
		kOpaque      = 1,
		kAlphaTest   = 2,
		kTransparent = 3
	};

	constexpr std::string_view
	getLayerTypeName(LayerType layerType)
	{
		switch (layerType)
		{
		case LayerType::kBackground:
			return "Background"sv;
		case LayerType::kOpaque:
			return "Opaque"sv;
		case LayerType::kAlphaTest:
			return "AlphaTest"sv;
		case LayerType::kTransparent:
			return "Transparent"sv;
		default:
			return "Invalid"sv;
		}
	}
}
