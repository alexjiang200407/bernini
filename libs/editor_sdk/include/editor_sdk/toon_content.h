#pragma once
#include <bgl/IGraphics.h>
#include <bgl/SurfaceType.h>
#include <bgl/types/MaterialHandle.h>
#include <span>

namespace editor
{
	/**
	 * Whether `material` is drawn by one of the toon models -- a surface registered on
	 * IToonCharacterSurfaceSource or IToonEnvironmentSurfaceSource. What a preview asks to decide
	 * it shows a toon look, which is authored for Standard tone mapping.
	 */
	[[nodiscard]] inline bool
	IsToonMaterial(const bgl::IGraphics& graphics, const bgl::MaterialHandle material) noexcept
	{
		if (!material.IsValid())
			return false;

		for (const bgl::SurfaceType& type : graphics.GetSurfaceTypes())
		{
			if (type.kind == material.materialType)
			{
				return type.shading == bgl::SurfaceShading::kToonCharacter ||
				       type.shading == bgl::SurfaceShading::kToonEnvironment;
			}
		}
		return false;
	}

	/** Whether any of `materials` is drawn by a toon model; see IsToonMaterial. */
	[[nodiscard]] inline bool
	AnyToonMaterial(
		const bgl::IGraphics&                graphics,
		std::span<const bgl::MaterialHandle> materials) noexcept
	{
		for (const bgl::MaterialHandle material : materials)
		{
			if (IsToonMaterial(graphics, material))
				return true;
		}
		return false;
	}
}
