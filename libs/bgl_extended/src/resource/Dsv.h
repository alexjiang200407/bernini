#pragma once
#include "types/Format.h"
#include "types/TextureDimension.h"

namespace bgl
{
	class Dsv;

	struct DsvDesc
	{
		Format           format          = Format::UNKNOWN;
		TextureDimension dimension       = TextureDimension::kTexture2D;
		uint32_t         mipSlice        = 0;
		uint32_t         firstArraySlice = 0;
		uint32_t         arraySize       = 1;

		std::string debugName = "";
	};

	struct DsvHandle
	{
		uint32_t idx        = 0xFFFFFFFF;
		uint32_t generation = 0;

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return idx == 0xFFFFFFFF;
		}

		operator bool() const noexcept { return !IsNull(); }
	};
}
