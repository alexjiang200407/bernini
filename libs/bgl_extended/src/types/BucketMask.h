#pragma once
#include <bgl_common/idl/Bucket.h>
#include <bitset>

namespace bgl
{
	// One bit per bucket -- the (tier x layer x material kind) group one indirect dispatch
	// draws, see docs/passes.md -- ceiling-sized like every count-sized structure.
	using BucketMask = std::bitset<idl::cMaxBuckets>;
}
