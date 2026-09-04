#pragma once

#include <cstdint>
namespace assetlib
{
	/**
	 * A source file as it stood when something was baked from it, recorded per route.
	 *
	 * Content rather than mtime: a checkout rewrites mtimes without changing a byte, and a stamp
	 * that noticed would re-bake every asset and dirty the containers in git. A hash also makes a
	 * bake reproducible, where an mtime is local to the machine that ran it.
	 *
	 * `stampOf` is what fills this in, and it memoizes, so re-reading a source it has already
	 * hashed costs a stat.
	 */
	struct SourceStamp
	{
		uint64_t size = 0;
		uint64_t hash = 0;  // core::file::hash_file over the whole file

		friend bool
		operator==(const SourceStamp&, const SourceStamp&) = default;
	};
}
