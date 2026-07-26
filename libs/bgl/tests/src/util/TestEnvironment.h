#pragma once

#include <bgl/IScene.h>

namespace bgl_test
{
	/**
	 * The IBL triplet every PBR render test needs, from `assets/forest.benv` plus the shared BRDF LUT.
	 *
	 * PBR does not render without an environment -- there is no default -- so this exists to keep a
	 * dozen test cases from each spelling out the same three loads, and to give one place to change
	 * when the environment asset does.
	 *
	 * @throws std::runtime_error if the `.benv` or the LUT cannot be read.
	 */
	[[nodiscard]] bgl::EnvironmentMapDesc
	LoadEnvironment(bgl::IScene* scene);

	/** The unfiltered environment from the same `.benv`, for the skybox pass. */
	[[nodiscard]] bgl::TextureAssetHandle
	LoadSkybox(bgl::IScene* scene);
}
