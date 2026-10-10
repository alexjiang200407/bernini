#pragma once
#include <cstdint>
#include <terrainlib/types/ErosionDesc.h>
#include <utility>

namespace terrain
{
	/** The lie of the land a field is generated with. */
	enum class TerrainShape : uint8_t
	{
		kFlat,         // a plain with a few metres of undulation
		kHilly,        // rolling hills a few tens of metres high
		kMountainous,  // ridged peaks a few hundred metres high
	};

	/**
	 * Samples a field may have along either axis: the renderer's ceiling for a terrain
	 * (`bgl::c_MaxTerrainSamples`), declared twice because this library links no renderer.
	 */
	constexpr uint32_t c_MaxGenerateSamples = 8192;

	/**
	 * What Generate makes a field from -- see it. The same desc gives the same field run after
	 * run on a machine; across compilers the float arithmetic may differ in its last bits, so a
	 * field two machines must share is stored, not regenerated.
	 */
	struct TerrainGenerateDesc
	{
		TerrainShape shape = TerrainShape::kHilly;
		uint32_t     seed  = 1;

		uint32_t samplesX = 1025;
		uint32_t samplesZ = 1025;

		// World units between samples. A shape's features are sized in world units, so a finer
		// cell resolves the same hills more closely rather than making smaller ones.
		float cellSize = 2.0f;

		// A scale on the shape's heights, applied before anything else reads them: below 1 the
		// hills are gentler than the shape's own, and every slope is measured on the scaled ground.
		float relief = 1.0f;

		// How the noise is worn down once generated; the default leaves it as generated.
		ErosionDesc erosion;

		template <typename Self>
		Self&&
		SetShape(this Self&& self, TerrainShape value) noexcept
		{
			self.shape = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSeed(this Self&& self, uint32_t value) noexcept
		{
			self.seed = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetSamples(this Self&& self, uint32_t x, uint32_t z) noexcept
		{
			self.samplesX = x;
			self.samplesZ = z;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetCellSize(this Self&& self, float value) noexcept
		{
			self.cellSize = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetErosion(this Self&& self, const ErosionDesc& value) noexcept
		{
			self.erosion = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetRelief(this Self&& self, float value) noexcept
		{
			self.relief = value;
			return std::forward<Self>(self);
		}
	};
}
