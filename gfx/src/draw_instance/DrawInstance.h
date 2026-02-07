#pragma once
#include "draw_instance/SortKey.h"

namespace gfx
{
	struct DrawInstance final
	{
		using ID = uint32_t;
		SortKey  sortKey;
		uint32_t dataIndex;
	};
}
