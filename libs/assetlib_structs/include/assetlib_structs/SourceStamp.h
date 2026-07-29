#pragma once

namespace assetlib
{
	/**
	 * A source file as it stood when something was baked from it, cheap enough to record per route.
	 *
	 * Size and mtime rather than a content hash: a bake compares these on every load of every asset,
	 * and hashing megabytes of texture to answer "has this changed" would cost more than the bake it
	 * is trying to avoid.
	 */
	struct SourceStamp
	{
		uint64_t size  = 0;
		int64_t  mtime = 0;  // seconds since the filesystem clock's epoch

		friend bool
		operator==(const SourceStamp&, const SourceStamp&) = default;
	};
}
