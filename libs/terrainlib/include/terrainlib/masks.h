#pragma once
#include <assetlib_structs/Heightfield.h>
#include <terrainlib/types/MaskDesc.h>
#include <terrainlib/types/TerrainFields.h>
#include <terrainlib/types/TerrainLayer.h>
#include <terrainlib/types/TerrainMasks.h>

namespace terrain
{
	/**
	 * The masks of `field`, whose fields are `fields`, by `desc`'s rules. Water first, from the
	 * fields alone; then woods off the water, cleaned -- woods under the rule's minimum area
	 * dropped, clearings under its minimum filled -- and the signed distance to a wood's edge by
	 * exact Euclidean distance transforms (Felzenszwalb and Huttenlocher 2012); then rock off both, under its
	 * minimum area dropped. A wood or an outcrop is a group of samples joined along either axis or
	 * diagonally. A mask painted by hand later takes the noise's place and keeps the cleanup.
	 *
	 * Deterministic from `desc.seed`; linear in the samples but for a selection per kind.
	 *
	 * @pre `fields` is DeriveFields(field).
	 * @throws std::runtime_error naming the first field of `desc` outside the range its comment
	 *         gives: a coverage outside [0, 1], or a size, area, scale or slope not finite and
	 *         positive.
	 */
	[[nodiscard]] TerrainMasks
	GenerateMasks(
		const assetlib::Heightfield& field,
		const TerrainFields&         fields,
		const MaskDesc&              desc);

	/**
	 * GenerateMasks with the water given rather than found: `water` is 1 where it stands, laid as
	 * `field` is, and `desc.water` goes unread. What a field whose water CarveWater cut takes, the
	 * margin it keeps woods and rocks back from the shore included.
	 *
	 * @throws std::runtime_error as above, and when `water` holds a value count other than the
	 *         field's.
	 */
	[[nodiscard]] TerrainMasks
	GenerateMasks(
		const assetlib::Heightfield& field,
		const TerrainFields&         fields,
		const MaskDesc&              desc,
		const TerrainLayer&          water);
}
