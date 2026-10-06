#include "noise.h"
#include <algorithm>
#include <assetlib_structs/Heightfield.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <core/parallel_for.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <terrainlib/Generate.h>
#include <vector>

namespace terrain
{
	namespace
	{
		/** How a shape is built from the noises: every length in world units. */
		struct ShapeParams
		{
			float    amplitude;   // the base noise's reach above and below the mean
			float    wavelength;  // of the base octave
			uint32_t octaves;
			float    gain;
			float    ridgeShare;  // how much of the height the ridged noise contributes, in [0, 1]
			float    warp;  // how far the domain is pushed about, as a share of the wavelength
		};

		constexpr float c_Lacunarity = 2.0f;

		[[nodiscard]] ShapeParams
		ParamsOf(const Shape shape) noexcept
		{
			switch (shape)
			{
			case Shape::kFlat:
				return { .amplitude  = 4.0f,
					     .wavelength = 500.0f,
					     .octaves    = 3,
					     .gain       = 0.5f,
					     .ridgeShare = 0.0f,
					     .warp       = 0.0f };
			case Shape::kHilly:
				return { .amplitude  = 45.0f,
					     .wavelength = 450.0f,
					     .octaves    = 5,
					     .gain       = 0.5f,
					     .ridgeShare = 0.0f,
					     .warp       = 0.35f };
			case Shape::kMountainous:
				return { .amplitude  = 300.0f,
					     .wavelength = 1200.0f,
					     .octaves    = 6,
					     .gain       = 0.55f,
					     .ridgeShare = 0.75f,
					     .warp       = 0.5f };
			}
			core::fatal("An unknown terrain shape");
		}

		/** The height at world `xz`, before the field is offset to its minimum. */
		[[nodiscard]] float
		HeightAt(const glm::vec2 xz, const ShapeParams& params, const uint32_t seed) noexcept
		{
			glm::vec2 q = xz / params.wavelength;

			if (params.warp > 0.0f)
			{
				const glm::vec2 push(
					Fbm(q + glm::vec2(5.2f, 1.3f), seed ^ 0x1b873593u, 3, c_Lacunarity, 0.5f),
					Fbm(q + glm::vec2(1.7f, 9.2f), seed ^ 0xcc9e2d51u, 3, c_Lacunarity, 0.5f));
				q += params.warp * push;
			}

			const float rolling = Fbm(q, seed, params.octaves, c_Lacunarity, params.gain);
			if (params.ridgeShare <= 0.0f)
			{
				return params.amplitude * rolling;
			}

			const float ridged =
				2.0f * Ridged(q, seed ^ 0xe6546b64u, params.octaves, c_Lacunarity, params.gain) -
				1.0f;
			return params.amplitude * glm::mix(rolling, ridged, params.ridgeShare);
		}

		void
		Validate(const GenerateDesc& desc)
		{
			if (desc.samplesX < 2 || desc.samplesZ < 2 || desc.samplesX > c_MaxGenerateSamples ||
			    desc.samplesZ > c_MaxGenerateSamples)
			{
				core::throw_runtime_error(
					"terrain::Generate: {} x {} samples; each axis takes 2 to {}",
					desc.samplesX,
					desc.samplesZ,
					c_MaxGenerateSamples);
			}
			if (!std::isfinite(desc.cellSize) || desc.cellSize <= 0.0f)
			{
				core::throw_runtime_error(
					"terrain::Generate: cellSize must be finite and positive");
			}
		}
	}

	assetlib::Heightfield
	Generate(const GenerateDesc& desc)
	{
		Validate(desc);

		const ShapeParams params = ParamsOf(desc.shape);
		const size_t      count  = static_cast<size_t>(desc.samplesX) * desc.samplesZ;

		// Rows in parallel: each writes its own run and its own extremes, and nothing is shared.
		auto heights = std::vector<float>(count);
		auto rowMin  = std::vector<float>(desc.samplesZ, std::numeric_limits<float>::max());
		auto rowMax  = std::vector<float>(desc.samplesZ, std::numeric_limits<float>::lowest());
		core::parallel_for(desc.samplesZ, 0, "terrain generate", [&](const size_t z) {
			float* row = heights.data() + z * desc.samplesX;
			for (uint32_t x = 0; x < desc.samplesX; ++x)
			{
				const glm::vec2 xz(
					static_cast<float>(x) * desc.cellSize,
					static_cast<float>(z) * desc.cellSize);
				row[x]    = HeightAt(xz, params, desc.seed);
				rowMin[z] = std::min(rowMin[z], row[x]);
				rowMax[z] = std::max(rowMax[z], row[x]);
			}
		});

		const float lowest  = *std::ranges::min_element(rowMin);
		const float highest = *std::ranges::max_element(rowMax);
		// A field with no relief still spans something, so a sample decodes to one height.
		const float range = std::max(highest - lowest, 1e-3f);

		auto field        = assetlib::Heightfield();
		field.samplesX    = desc.samplesX;
		field.samplesZ    = desc.samplesZ;
		field.cellSize    = desc.cellSize;
		field.minHeight   = lowest;
		field.heightRange = range;
		field.heights.resize(count);
		for (size_t i = 0; i < count; ++i)
		{
			const float share = glm::clamp((heights[i] - lowest) / range, 0.0f, 1.0f);
			field.heights[i]  = static_cast<uint16_t>(std::lround(share * 65535.0f));
		}
		return field;
	}
}
