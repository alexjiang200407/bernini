#pragma once
#include <bgl_common/idl/PsoType.h>
#include <bitset>

namespace bgl
{
	// One bit per pso row, ceiling-sized like every count-sized structure.
	using PsoRowMask = std::bitset<idl::cMaxPsoBuckets>;
}
