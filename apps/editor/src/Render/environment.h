#pragma once

#include <bgl/IScene.h>
#include <bgl/ISceneView.h>

namespace editor
{
	/**
	 * Puts a `.benv`'s image-based lighting onto a view: the IBL pair, the skybox, and the exposure.
	 *
	 * Shared by the material preview and the thumbnail cache so the two cannot light the same asset
	 * differently -- a thumbnail that disagrees with the preview it was generated from is a bug that
	 * only shows up side by side.
	 *
	 * Degrades rather than throws: a missing or unreadable environment warns and leaves the view
	 * unlit. That is deliberately survivable, because an editor that will not open is worse than one
	 * that draws dark -- but it is also why a broken path is quiet, so check the log if a viewport is
	 * black.
	 *
	 * Must be called on the render thread, like everything else that touches a scene or a view.
	 *
	 * @param benvPath The `.benv`; nothing is applied when empty.
	 * @param exposureOverride Overrules the exposure the `.benv` derived for these maps.
	 * @param who Prefix for warnings, naming the caller.
	 */
	void
	ApplyEnvironment(
		bgl::IScene*         scene,
		bgl::ISceneView*     view,
		const std::string&   benvPath,
		std::optional<float> exposureOverride,
		const char*          who);
}
