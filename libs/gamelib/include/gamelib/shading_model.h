#pragma once
#include <assetlib_structs/BMaterial.h>
#include <bgl/SurfaceType.h>

namespace game
{
	/**
	 * The document model for a surface on `shading`'s contract: what the editor's surface sink
	 * writes, so a save says what the surface's own module says. Inverse of ToSurfaceShading.
	 */
	[[nodiscard]] constexpr assetlib::ShadingModel
	ToShadingModel(bgl::SurfaceShading shading) noexcept
	{
		return shading == bgl::SurfaceShading::kLit ? assetlib::ShadingModel::kLitSurface :
		                                              assetlib::ShadingModel::kPbrSurface;
	}

	/**
	 * The contract a surface document's model expects, which the renderer checks against the
	 * registered surface (ADR-5).
	 *
	 * @pre `assetlib::isSurfaceModel(model)` — the PBR triplet model expects no contract.
	 */
	[[nodiscard]] constexpr bgl::SurfaceShading
	ToSurfaceShading(assetlib::ShadingModel model) noexcept
	{
		return model == assetlib::ShadingModel::kLitSurface ? bgl::SurfaceShading::kLit :
		                                                      bgl::SurfaceShading::kPbrSurface;
	}
}
