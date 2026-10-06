#pragma once
#include <assetlib_structs/Heightfield.h>
#include <cstdint>
#include <utility>

namespace terrain
{
	/** The lie of the land a field is generated with. */
	enum class Shape : uint8_t
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
	struct GenerateDesc
	{
		Shape    shape = Shape::kHilly;
		uint32_t seed  = 1;

		uint32_t samplesX = 1025;
		uint32_t samplesZ = 1025;

		// World units between samples. A shape's features are sized in world units, so a finer
		// cell resolves the same hills more closely rather than making smaller ones.
		float cellSize = 2.0f;

		template <typename Self>
		Self&&
		SetShape(this Self&& self, Shape value) noexcept
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
	};

	/**
	 * A heightfield of `desc`'s shape, from fractal gradient noise -- ridged for the mountainous
	 * shape -- over a warped domain, seeded by `desc.seed`. Deterministic as the desc says, and a
	 * different seed is a different field of the same character. The field's
	 * `minHeight` and `heightRange` are the lowest and the span of what was generated, so its
	 * 16-bit samples use their whole range.
	 *
	 * Cost is linear in the samples, spread over the hardware threads.
	 *
	 * @throws std::runtime_error if either sample count is below 2 or above
	 *         c_MaxGenerateSamples, or `cellSize` is not finite and positive.
	 */
	[[nodiscard]] assetlib::Heightfield
	Generate(const GenerateDesc& desc);
}
