#pragma once

#include <bgl/IGraphics.h>
#include <bgl/IRenderTarget.h>
#include <core/glm.h>

namespace bgl::test
{
	/**
	 * The sRGB transfer function the backbuffer applies on write. AgX linearizes its own
	 * display-encoded result so this re-encode is the last step, and it is where the number a person
	 * would sample off a screenshot finally appears.
	 */
	[[nodiscard]] float
	EncodeSrgb(float linear) noexcept;

	/**
	 * AgX(grey) as the shipped tone map computes it, in scene-linear output: one dispatch of the
	 * suite's CSAgxCalibration kernel on `gfx`.
	 *
	 * Where a render test needs to know what a scene-linear value *should* land at on screen, this
	 * is the only answer that cannot drift from the shader, because it is the shader.
	 */
	[[nodiscard]] glm::vec4
	RunAgX(bgl::IGraphics& gfx, float sceneLinear);

	/**
	 * `sceneLinear` at output position `uv` through the post pass's grade and AgX, with `settings`
	 * converted to constants the way the pass converts them: one dispatch of CSColorGradeProbe.
	 */
	[[nodiscard]] glm::vec4
	RunGradedAgX(
		bgl::IGraphics&                gfx,
		glm::vec3                      sceneLinear,
		glm::vec2                      uv,
		const bgl::ColorGradeSettings& settings);
}
