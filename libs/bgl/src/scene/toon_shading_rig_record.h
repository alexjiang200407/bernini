#pragma once
#include <bgl/idl/ToonShadingRig.h>
#include <bgl/types/ToonShadingRigDesc.h>
#include <vector>

namespace bgl
{
	/** A toon shading rig as AddToonShadingRig uploads it: the record, and its edits' keys in edit order. */
	struct PackedToonShadingRig
	{
		// `keys` is left null: it names the range the keys are uploaded into, which only the
		// caller knows. Each edit's `firstKey` is relative to it.
		idl::ToonShadingRig record;

		std::vector<idl::ToonShadingRigKey> keys;
	};

	/**
	 * Flattens `desc` into what the evaluation pass reads: each key's light normalized, each edit's
	 * sign lock read off its keys' gains and its mirrored flag set, `headToBone` as rows.
	 *
	 * @pre `desc` passed AddToonShadingRig's validation.
	 */
	[[nodiscard]] PackedToonShadingRig
	PackToonShadingRig(const ToonShadingRigDesc& desc);
}
