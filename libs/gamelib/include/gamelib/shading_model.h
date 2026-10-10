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
		switch (shading)
		{
		case bgl::SurfaceShading::kPbrSurface:
			break;
		case bgl::SurfaceShading::kLit:
			return assetlib::ShadingModel::kLitSurface;
		case bgl::SurfaceShading::kToonCharacter:
			return assetlib::ShadingModel::kToonCharacterSurface;
		case bgl::SurfaceShading::kWater:
			return assetlib::ShadingModel::kWaterSurface;
		}
		return assetlib::ShadingModel::kPbrSurface;
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
		switch (model)
		{
		case assetlib::ShadingModel::kLitSurface:
			return bgl::SurfaceShading::kLit;
		case assetlib::ShadingModel::kToonCharacterSurface:
			return bgl::SurfaceShading::kToonCharacter;
		case assetlib::ShadingModel::kWaterSurface:
			return bgl::SurfaceShading::kWater;
		case assetlib::ShadingModel::kPbr:
		case assetlib::ShadingModel::kPbrSurface:
		case assetlib::ShadingModel::kCount:
			break;
		}
		return bgl::SurfaceShading::kPbrSurface;
	}
}
