#pragma once
#include "types/Format.h"

namespace bgl
{
	enum class FormatKind : uint8_t
	{
		kInteger,
		kNormalized,
		kFloat,
		kDepthStencil
	};

	struct FormatInfo
	{
		Format      format;
		const char* name;
		uint8_t     bytesPerBlock;
		uint8_t     blockEdgeTexels;  // 1 for uncompressed formats, 4 for BC blocks
		FormatKind  kind;
		bool        hasRed    : 1;
		bool        hasGreen  : 1;
		bool        hasBlue   : 1;
		bool        hasAlpha  : 1;
		bool        hasDepth  : 1;
		bool        hasStencil: 1;
		bool        isSigned  : 1;
		bool        isSRGB    : 1;
	};
}
