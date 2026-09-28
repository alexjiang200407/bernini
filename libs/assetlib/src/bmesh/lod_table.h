#pragma once
#include <assetlib_structs/BMesh.h>
#include <assetlib_structs/BMeshImport.h>
#include <cstdint>
#include <span>

namespace assetlib
{
	/** The default threshold of level 0: a mesh drops to level 1 once it spans fewer pixels. */
	inline constexpr float c_DefaultLodZeroMinPixels = 160.0f;

	/**
	 * The threshold a level takes when its document authors none: c_DefaultLodZeroMinPixels for
	 * level 0, halving with each level after, and 0 -- never dropped -- for the last.
	 */
	[[nodiscard]] float
	defaultLodMinPixels(uint32_t level, uint32_t lodCount) noexcept;

	/**
	 * Rebuilds `mesh.lods` and every mesh entry's `firstLod` from its `lodCount`: entry n is
	 * `authored[n]` where the list reaches and the default beyond. Every mesh gets a table once
	 * any mesh has a level past 0 or the list authors one, since an empty table is what says
	 * "one level, drawn at every size"; otherwise `lods` is left empty.
	 *
	 * @throws std::runtime_error if `authored` lists more levels than a mesh carries, or if the
	 *         authored entries and the defaults after them rise from one level to the next.
	 */
	void
	writeLodTables(BMesh& mesh, std::span<const float> authored);

	/** writeLodTables for an import, as the cook folds its levels. */
	void
	writeLodTables(imp::BMeshImport& mesh, std::span<const float> authored);
}
