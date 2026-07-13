#pragma once
#include "idl/Entry.h"
#include <bgl/PsoType.h>

namespace bgl
{
	/**
	 * One drawable: a submesh of a placed mesh instance, with its resolved shading. `material` and
	 * `pso` are the geom's defaults unless this instance overrides them. The counting sort buckets on
	 * `pso`, so two instances of one submesh may draw from different pipelines.
	 */
	struct SubmeshInstance
	{
		idl::Entry meshInstance;
		uint32_t   submeshIndex = 0;
		idl::Entry material;

		// kInvalid, not 0: the sort skips a pso >= kCount, which is what keeps tail padding out of a
		// real bucket.
		uint32_t pso = static_cast<uint32_t>(PsoType::kInvalid);
	};
}
