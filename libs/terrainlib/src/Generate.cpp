#include "erode.h"
#include "grids.h"
#include <algorithm>

#include <assetlib_structs/Heightfield.h>
#include <cmath>
#include <core/err/util.h>
#include <core/glm.h>
#include <core/hash.h>
#include <core/noise.h>
#include <core/parallel_for.h>
#include <cstddef>
#include <cstdint>
#include <terrainlib/Generate.h>
#include <terrainlib/types/TerraceDesc.h>
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

		// The share of a minor step its strata are offset by from the major ones, so the two never
		// share an edge.
		constexpr float c_MinorPhase = 0.37f;

		[[nodiscard]] ShapeParams
		ParamsOf(const TerrainShape shape) noexcept
		{
			switch (shape)
			{
			case TerrainShape::kFlat:
				return { .amplitude  = 4.0f,
					     .wavelength = 500.0f,
					     .octaves    = 3,
					     .gain       = 0.5f,
					     .ridgeShare = 0.0f,
					     .warp       = 0.0f };
			case TerrainShape::kHilly:
				return { .amplitude  = 45.0f,
					     .wavelength = 450.0f,
					     .octaves    = 5,
					     .gain       = 0.5f,
					     .ridgeShare = 0.0f,
					     .warp       = 0.35f };
			case TerrainShape::kMountainous:
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
					core::fbm(q + glm::vec2(5.2f, 1.3f), seed ^ 0x1b873593u, 3, c_Lacunarity, 0.5f),
					core::fbm(
						q + glm::vec2(1.7f, 9.2f),
						seed ^ 0xcc9e2d51u,
						3,
						c_Lacunarity,
						0.5f));
				q += params.warp * push;
			}

			const float rolling = core::fbm(q, seed, params.octaves, c_Lacunarity, params.gain);
			if (params.ridgeShare <= 0.0f)
			{
				return params.amplitude * rolling;
			}

			const float ridged = 2.0f * core::ridged_noise(
											q,
											seed ^ 0xe6546b64u,
											params.octaves,
											c_Lacunarity,
											params.gain) -
			                     1.0f;
			return params.amplitude * glm::mix(rolling, ridged, params.ridgeShare);
		}

		/**
		 * A step's height at `u`, `step` metres a step: a shelf that climbs `rise` of the step over
		 * the first `shelf` of it, then a face that leaves the shelf at a scree's angle, steepens
		 * and rounds over at its lip, meeting the next shelf with no seam.
		 */
		[[nodiscard]] float
		Stepped(const float u, const float step, const float shelf, const float rise) noexcept
		{
			const float t    = u / step;
			const float k    = std::floor(t);
			const float f    = t - k;
			const float face = glm::clamp((f - shelf) / (1.0f - shelf), 0.0f, 1.0f);
			const float lift = rise * std::min(f / shelf, 1.0f) +
			                   (1.0f - rise) * glm::smoothstep(0.0f, 1.0f, std::pow(face, 1.5f));
			return (k + lift) * step;
		}

		/** Steps `heights`, laid as `desc`'s samples, into the strata `desc.terrace` says. */
		void
		Terrace(std::vector<float>& heights, const TerrainGenerateDesc& desc)
		{
			const TerraceDesc& terrace = desc.terrace;
			const float        lowest  = std::ranges::min(heights);
			const float     angle = static_cast<float>(core::hash_mix32(desc.seed ^ 0x5bd1e995u)) *
			                        (6.2831853f / 4294967296.0f);
			const glm::vec2 dip   = glm::vec2(std::cos(angle), std::sin(angle)) * terrace.tilt;
			const int       radius =
				static_cast<int>(std::lround(terrace.smoothing / desc.cellSize * 0.5f));
			// Stepped raw, every bump of the noise moves a face's edge and flutes it.
			const std::vector<float> broad =
				radius > 0 ? BoxMean(
								 BoxMean(heights, desc.samplesX, desc.samplesZ, radius),
								 desc.samplesX,
								 desc.samplesZ,
								 radius) :
							 heights;

			core::parallel_for(desc.samplesZ, 0, "terrain terrace", [&](const size_t z) {
				float*       row  = heights.data() + z * desc.samplesX;
				const float* land = broad.data() + z * desc.samplesX;
				for (uint32_t x = 0; x < desc.samplesX; ++x)
				{
					const float above  = land[x] - lowest;
					const float weight = glm::smoothstep(
						terrace.startHeight,
						terrace.startHeight + terrace.fadeHeight,
						above);
					if (weight <= 0.0f)
						continue;

					const glm::vec2 xz(
						static_cast<float>(x) * desc.cellSize,
						static_cast<float>(z) * desc.cellSize);
					const glm::vec2 q = xz / terrace.noiseWavelength;
					const float     wander =
						core::fbm(q, desc.seed ^ 0x68e31da4u, 3, c_Lacunarity, 0.5f) *
						terrace.edgeNoise;
					// Clamped, since fbm is only about [-1, 1]: a step never reaches zero height.
					const float spread = glm::clamp(
						core::fbm(q * 0.25f, desc.seed ^ 0xb5297a4du, 2, c_Lacunarity, 0.5f),
						-1.0f,
						1.0f);
					const float step = terrace.stepHeight * (1.0f + terrace.jitter * spread);

					const float u       = above + glm::dot(xz, dip) + wander;
					float       stepped = Stepped(u, step, terrace.shelf, terrace.shelfRise);
					if (terrace.minorStep > 0.0f)
					{
						const float minorHeight = step * terrace.minorStep;
						const float minorU      = u + c_MinorPhase * minorHeight;
						stepped += terrace.minorStrength *
						           (Stepped(minorU, minorHeight, terrace.shelf, terrace.shelfRise) -
						            minorU);
					}
					const float detail = row[x] - land[x];
					row[x] += weight * ((stepped - u) - (1.0f - terrace.detail) * detail);
				}
			});

			// A lip sharper than a cell draws as a staircase of triangles.
			const std::vector<float> rounded = BoxMean(heights, desc.samplesX, desc.samplesZ, 1);
			for (size_t i = 0; i < heights.size(); ++i)
			{
				const float weight = glm::smoothstep(
					terrace.startHeight,
					terrace.startHeight + terrace.fadeHeight,
					broad[i] - lowest);
				heights[i] = glm::mix(heights[i], rounded[i], weight);
			}
		}

		void
		ValidateTerrace(const TerraceDesc& desc)
		{
			const auto refuse = [](const char* field) {
				core::throw_runtime_error(
					"terrain::Generate: terrace.{} is outside its range",
					field);
			};
			const auto finite = [](const float v) { return std::isfinite(v); };
			if (!finite(desc.stepHeight) || desc.stepHeight < 0.0f)
				refuse("stepHeight");
			if (!desc.Terraces())
				return;
			if (!finite(desc.shelf) || desc.shelf <= 0.0f || desc.shelf >= 1.0f)
				refuse("shelf");
			if (!finite(desc.shelfRise) || desc.shelfRise < 0.0f || desc.shelfRise >= 1.0f)
				refuse("shelfRise");
			if (!finite(desc.jitter) || desc.jitter < 0.0f || desc.jitter >= 1.0f)
				refuse("jitter");
			if (!finite(desc.tilt))
				refuse("tilt");
			if (!finite(desc.edgeNoise) || desc.edgeNoise < 0.0f)
				refuse("edgeNoise");
			if (!finite(desc.noiseWavelength) || desc.noiseWavelength <= 0.0f)
				refuse("noiseWavelength");
			if (!finite(desc.smoothing) || desc.smoothing < 0.0f)
				refuse("smoothing");
			if (!finite(desc.detail) || desc.detail < 0.0f || desc.detail > 1.0f)
				refuse("detail");
			if (!finite(desc.minorStep) || desc.minorStep < 0.0f || desc.minorStep >= 1.0f)
				refuse("minorStep");
			if (!finite(desc.minorStrength) || desc.minorStrength < 0.0f ||
			    desc.minorStrength > 1.0f)
				refuse("minorStrength");
			if (!finite(desc.startHeight) || desc.startHeight < 0.0f)
				refuse("startHeight");
			if (!finite(desc.fadeHeight) || desc.fadeHeight <= 0.0f)
				refuse("fadeHeight");
		}

		void
		Validate(const TerrainGenerateDesc& desc)
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
			if (!std::isfinite(desc.relief) || desc.relief <= 0.0f)
			{
				core::throw_runtime_error("terrain::Generate: relief must be finite and positive");
			}
			ValidateTerrace(desc.terrace);
			ValidateErosion(desc.erosion);
		}
	}

	assetlib::Heightfield
	Generate(const TerrainGenerateDesc& desc)
	{
		Validate(desc);

		ShapeParams params = ParamsOf(desc.shape);
		params.amplitude *= desc.relief;
		const size_t count = static_cast<size_t>(desc.samplesX) * desc.samplesZ;

		// Rows in parallel: each writes its own run, and nothing is shared.
		auto heights = std::vector<float>(count);
		core::parallel_for(desc.samplesZ, 0, "terrain generate", [&](const size_t z) {
			float* row = heights.data() + z * desc.samplesX;
			for (uint32_t x = 0; x < desc.samplesX; ++x)
			{
				const glm::vec2 xz(
					static_cast<float>(x) * desc.cellSize,
					static_cast<float>(z) * desc.cellSize);
				row[x] = HeightAt(xz, params, desc.seed);
			}
		});

		if (desc.terrace.Terraces())
		{
			Terrace(heights, desc);
		}

		if (desc.erosion.Erodes())
		{
			Erode(heights, desc.samplesX, desc.samplesZ, desc.cellSize, desc.erosion, desc.seed);
		}

		const auto [lowest, highest] = std::ranges::minmax(heights);
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
