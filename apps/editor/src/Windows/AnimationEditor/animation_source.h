#pragma once

namespace editor
{
	/**
	 * Which tier the Animation panel previews a rig through.
	 *
	 * Both are real: the skinned pose is what the rig says, and the VAT pose is what the bake made of
	 * it. Being able to switch is how a bake that dropped or smeared something gets caught before it
	 * reaches the game -- a `.bvat` is a build product with no other viewer.
	 */
	enum class AnimationSource
	{
		kSkinned,
		kVat,
	};
}
