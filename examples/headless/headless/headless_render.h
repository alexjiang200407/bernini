#pragma once
#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <bgl/IScene.h>
#include <bgl/ISceneView.h>
#include <bgl/glm.h>
#include <cstdint>
#include <filesystem>
#include <gamelib/AssetManager.h>
#include <string_view>

namespace headless
{
	/**
	 * A device sized for one model and its environment, logging errors only. A project's materials
	 * may shade through surfaces of its own, and those are registered only at creation, so the
	 * device registers whatever `dataRoot`'s Authored/Shaders holds -- none when it has none.
	 */
	[[nodiscard]] bgl::GraphicsRef
	CreateHeadlessGraphics(const std::filesystem::path& dataRoot);

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

	/**
	 * A sun placed on the sky dome, as the direction its light *travels* -- so, downward.
	 *
	 * The negation is the whole point of having this in one place: bgl::DirectionalLightDesc takes
	 * the direction light travels, not the direction the sun is in, and the two invert a scene
	 * silently when they are mixed.
	 *
	 * @param azimuth   Radians about the up axis, 0 along +Z and rising toward +X.
	 * @param elevation Radians above the horizon.
	 */
	[[nodiscard]] glm::vec3
	SunDirection(float azimuth, float elevation) noexcept;
}
