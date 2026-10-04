#pragma once
#include <bgl/types/ToonShadingRigDesc.h>
#include <cstdint>

namespace assetlib
{
	struct BToonShadingRig;
	struct Skeleton;
}

namespace game
{
	/** How the mesh a rig is drawn on holds its head bone. */
	enum class ToonShadingRigPose : uint8_t
	{
		kPosed,     // a skinned placement: the bone's pose is evaluated every frame
		kBindPose,  // static geometry of a skinned source: the bone stays where the bind put it
	};

	/**
	 * The renderer's desc for the `.btoonrig` `rig` on a mesh of `skeleton`: its angles from the
	 * document's degrees into radians, and its head bone from a name into what `bgl` takes.
	 *
	 * Posed, the bone becomes its index. In the bind pose there is nothing to pose, so the bone's
	 * bind transform is folded into `headToBone` and no bone is named -- the head sits where the
	 * static mesh drew it.
	 *
	 * @param skeleton The mesh's rig; null for a mesh with none, which only a rig naming no head
	 *        bone can be drawn on.
	 * @throws std::runtime_error naming the bone if the rig names one `skeleton` does not carry, or
	 *         names one and `skeleton` is null.
	 */
	[[nodiscard]] bgl::ToonShadingRigDesc
	ToonShadingRigDescOf(
		const assetlib::BToonShadingRig& rig,
		const assetlib::Skeleton*        skeleton,
		ToonShadingRigPose               pose);
}
