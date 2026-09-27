#pragma once
#include <assetlib_structs/Mesh.h>
#include <core/str/string_pool.h>
#include <cstdint>
#include <span>
#include <vector>

namespace assetlib
{
	/**
	 * The threshold a level takes when its document authors none: 160 pixels for level 0, halving
	 * with each level after, and 0 -- never dropped -- for the last.
	 */
	[[nodiscard]] float
	defaultLodMinPixels(uint32_t level, uint32_t lodCount) noexcept;

	/**
	 * Rebuilds `lods` and every mesh's `firstLod` from each mesh's `lodCount`: entry n is
	 * `authored[n]` where the list reaches and the default beyond. Every mesh gets a table once
	 * any mesh has a level past 0 or the list authors one, since an empty table is what says
	 * "one level, drawn at every size"; otherwise `lods` is left empty.
	 *
	 * @throws std::runtime_error if `authored` lists more levels than a mesh carries, or if the
	 *         authored entries and the defaults after them rise from one level to the next.
	 */
	void
	writeLodTables(
		std::span<Mesh>          meshes,
		std::vector<MeshLod>&    lods,
		std::span<const float>   authored,
		const core::string_pool& names);
}
