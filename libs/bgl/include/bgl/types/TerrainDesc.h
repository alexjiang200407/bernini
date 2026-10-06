#pragma once
#include <assetlib_structs/Heightfield.h>
#include <bgl/glm.h>
#include <bgl/types/MaterialHandle.h>
#include <cstdint>
#include <utility>

namespace bgl
{
	/** Samples a heightfield may have along either axis. The renderer names the same number. */
	constexpr uint32_t c_MaxTerrainSamples = 8192;

	/**
	 * A terrain -- see IScene::CreateTerrain. One heightfield laid on the ground, its sample (0, 0)
	 * at `origin` and its x and z axes the world's, drawn through `material` at the level of detail
	 * its cells' size on screen earns. The heightfield is copied by the call; the pointer is not
	 * kept.
	 */
	struct TerrainDesc
	{
		const assetlib::Heightfield* heightfield = nullptr;

		// World position of sample (0, 0) at a height of minHeight: the heights add to origin.y.
		glm::vec3 origin = glm::vec3(0.0f);

		// An opaque material, alive for as long as the terrain draws through it.
		MaterialHandle material;

		// The size on screen, in pixels, a cell is drawn at: the level of detail drawn where the
		// camera stands is the coarsest whose cells span no more than this. Scaled by the view's
		// LodSelectionDesc::pixelScale, as every authored threshold is.
		float pixelsPerCell = 6.0f;

		template <typename Self>
		Self&&
		SetHeightfield(this Self&& self, const assetlib::Heightfield* value) noexcept
		{
			self.heightfield = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetOrigin(this Self&& self, const glm::vec3& value) noexcept
		{
			self.origin = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetMaterial(this Self&& self, MaterialHandle value) noexcept
		{
			self.material = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetPixelsPerCell(this Self&& self, float value) noexcept
		{
			self.pixelsPerCell = value;
			return std::forward<Self>(self);
		}
	};
}
