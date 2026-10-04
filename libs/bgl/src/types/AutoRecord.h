#pragma once
#include <bgpu/idl/RawEntry.h>
#include <core/containers/multi_slot_handle.h>

namespace bgl
{
	/**
	 * A kAuto playback record in a view's arena and the foot-IK record it names, made and freed
	 * together: what one automatic placement owns, and what a skinned block's placements share.
	 * Either half is null when there is none.
	 */
	struct AutoRecord
	{
		bgpu::idl::RawEntry     record;
		core::multi_slot_handle footIK;
	};
}
