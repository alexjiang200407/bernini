#pragma once
#include <bgl_common/idl/DrawBucket.h>
#include <bitset>

namespace bgl
{
	// One bit per draw bucket -- the (tier x layer x material kind) group one indirect dispatch
	// draws -- ceiling-sized like every count-sized structure.
	using DrawBucketMask = std::bitset<idl::cMaxDrawBuckets>;
}
