#pragma once
#include "types/SortKey.h"

namespace gfx
{
	struct DrawInstance final
	{
		using ID = uint32_t;
		SortKey  sortKey;
		uint32_t geomSpecId     = 0;
		uint32_t materialSpecId = 0;
	};
}
