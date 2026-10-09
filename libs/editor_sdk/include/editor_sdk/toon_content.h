#pragma once
#include <bgl/IGraphics.h>
#include <bgl/SurfaceType.h>
#include <bgl/types/MaterialHandle.h>
#include <span>

namespace editor
{
	/**
	 * Whether `material` is drawn by the toon model -- a surface registered on
	 * IToonCharacterSurfaceSource, looked up among `surfaces` (IGraphics::GetSurfaceTypes). What a
	 * preview asks to decide it shows a toon look, which it draws against the toon backdrop.
	 */
	[[nodiscard]] inline bool
	IsToonMaterial(
		std::span<const bgl::SurfaceType> surfaces,
		const bgl::MaterialHandle         material) noexcept
	{
		if (!material.IsValid())
			return false;

		for (const bgl::SurfaceType& type : surfaces)
		{
			if (type.kind == material.materialType)
			{
				return type.shading == bgl::SurfaceShading::kToonCharacter;
			}
		}
		return false;
	}

	[[nodiscard]] inline bool
	IsToonMaterial(const bgl::IGraphics& graphics, const bgl::MaterialHandle material) noexcept
	{
		return IsToonMaterial(graphics.GetSurfaceTypes(), material);
	}

	/** Whether any of `materials` is drawn by the toon model; see IsToonMaterial. */
	[[nodiscard]] inline bool
	AnyToonMaterial(
		std::span<const bgl::SurfaceType>    surfaces,
		std::span<const bgl::MaterialHandle> materials) noexcept
	{
		for (const bgl::MaterialHandle material : materials)
		{
			if (IsToonMaterial(surfaces, material))
				return true;
		}
		return false;
	}

	[[nodiscard]] inline bool
	AnyToonMaterial(
		const bgl::IGraphics&                graphics,
		std::span<const bgl::MaterialHandle> materials) noexcept
	{
		return AnyToonMaterial(graphics.GetSurfaceTypes(), materials);
	}
}
