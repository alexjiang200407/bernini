#pragma once
#include <bgl_common/idl/Bucket.h>
#include <bgl_common/idl/Entry.h>
#include <bgl_common/idl/RawEntry.h>
#include <cstdint>

namespace bgl
{
	/**
	 * One drawable: a submesh of a placed mesh instance, with its resolved shading. `material` and
	 * `bucket` are the geom's defaults unless this instance overrides them. The counting sort buckets
	 * on `bucket`, so two instances of one submesh may draw from different pipelines.
	 */
	struct SubmeshInstance
	{
		idl::Entry meshInstance;
		uint32_t   submeshIndex = 0;

		// A byte offset into the scene's material arena, naming the record's header -- not an
		// element index. The record says which kind it is; `bucket` agrees by construction.
		idl::RawEntry material;

		// cInvalidBucket, not 0: the sort skips an id past the ceiling, which is what keeps tail
		// padding out of a real bucket.
		uint32_t bucket = idl::cInvalidBucket;
	};
}
