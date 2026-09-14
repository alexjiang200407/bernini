#pragma once
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <cstdint>
#include <gamelib/AssetManager.h>
#include <string_view>

namespace headless
{
	/** A device sized for one model and its environment, logging errors only. */
	[[nodiscard]] bgl::GraphicsRef
	CreateHeadlessGraphics();

	/** An offscreen target: presents nothing, and captures what it last drew. */
	[[nodiscard]] bgl::RenderTargetRef
	CreateHeadlessTarget(
		const bgl::GraphicsRef& graphics,
		uint32_t                width,
		uint32_t                height,
		bool                    taa);

	[[nodiscard]] bgl::SceneRef
	CreateHeadlessScene(const bgl::GraphicsRef& graphics);

	/**
	 * Lights `view` from the `.benv` at `envKey`, and skies it when the environment has a sky.
	 *
	 * A project is free to have no environment, and a model measured unlit is still worth more than
	 * a refusal -- so a failure is reported on stderr and the view is left unlit rather than thrown.
	 *
	 * @return whether `view` is lit.
	 */
	[[nodiscard]] bool
	LightView(const bgl::SceneViewRef& view, game::AssetManager& assets, std::string_view envKey);
}
