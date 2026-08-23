#pragma once
#include <assetlib_structs/SourceStamp.h>

namespace assetlib
{
	/**
	 * Where a derived container came from: the copied source's mount key, its content stamp, and
	 * the hash of the import document's parameter subtree — the inputs half of the cache key (the
	 * bake token is the engine's and never stored here).
	 *
	 * An empty key means no source was ever recorded — a synthetic fixture, or a file from before
	 * sources were copied. Such a container is current while its token holds and unrecoverable
	 * once it does not.
	 */
	struct SourceRef
	{
		std::string key;  // data-root-relative, "meshes_src/kirk.glb"
		SourceStamp stamp;
		uint64_t    parametersHash = 0;

		bool
		operator==(const SourceRef&) const = default;
	};
}
