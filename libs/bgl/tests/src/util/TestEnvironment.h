#pragma once

#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

namespace bgl_test
{
	/**
	 * Puts `assets/studio_kominka_02.benv`'s lighting on a view: the IBL triplet plus the exposure the
	 * environment was measured at.
	 *
	 * PBR does not render without an environment -- there is no default -- so this exists to keep a
	 * dozen cases from spelling out the same loads, and to give one place to change when the
	 * environment asset does.
	 *
	 * The exposure matters as much as the maps. An HDR environment's absolute scale is arbitrary, so a
	 * view left at 1.0 renders whatever the next environment's scale happens to be: swapping in one
	 * six times dimmer silently darkened every golden by six stops and tripped an absolute colour
	 * threshold in Transparent_test that had nothing to do with what it was testing.
	 *
	 * @throws std::runtime_error if the `.benv` or the BRDF LUT cannot be read.
	 */
	void
	ApplyEnvironment(bgl::IScene* scene, bgl::ISceneView* view);

	/** The unfiltered environment from the same `.benv`, for the skybox pass. */
	[[nodiscard]] bgl::TextureAssetHandle
	LoadSkybox(bgl::IScene* scene);
}
