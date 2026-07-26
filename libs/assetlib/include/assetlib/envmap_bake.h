#pragma once
#include <assetlib_structs/ImageData.h>

namespace assetlib
{
	struct PrefilterDesc
	{
		// Base face size of the output. Every mip halves from here.
		uint32_t faceSize = 256;

		// Must match the shader's MAX_REFLECTION_LOD + 1: roughness is mip / (mipLevels - 1), so a
		// different count silently remaps roughness. See docs/envmaps.md.
		uint32_t mipLevels = 7;

		// GGX samples per output texel. Cost is linear in this and independent of roughness.
		uint32_t samples = 128;

		// 0 means std::thread::hardware_concurrency().
		uint32_t threads = 0;
	};

	struct PrefilterStats
	{
		double   seconds       = 0.0;
		uint64_t texelsWritten = 0;
		uint64_t samplesTaken  = 0;
	};

	/**
	 * Convolves a radiance environment cube map with the clamped-cosine kernel, via a 9-coefficient
	 * spherical-harmonic projection, into the `irradianceMap` the diffuse term samples.
	 *
	 * Order 3 is not an approximation worth apologising for: Lambertian diffuse is so aggressive a
	 * low-pass that everything above l = 2 is discarded by the convolution itself, so 9 coefficients
	 * carry over 99% of the result. It is also cheaper than sampling -- one O(source) projection pass
	 * and O(1) per output texel, against a hemisphere integral per texel -- and has no Monte Carlo
	 * noise to trade against, unlike PrefilterRadiance.
	 *
	 * Stores **irradiance divided by pi**, i.e. the cosine-weighted average incident radiance, which
	 * is what the shader's `irradiance * albedo` expects and what makes this map's mean comparable to
	 * the prefilter's. A constant environment therefore round-trips to its own radiance.
	 *
	 * @param source A cube map in `R32G32B32A32_SFLOAT`; only mip 0 is read.
	 * @param faceSize Face size of the result. 128 is ample -- the signal is band-limited to l = 2.
	 * @return A cube map in `R32G32B32A32_SFLOAT` with one mip level.
	 * @throws std::runtime_error if `source` is not a float cube map, or `faceSize` is 0.
	 */
	[[nodiscard]] ImageData
	IrradianceSh(const ImageData& source, uint32_t faceSize = 128);

	/**
	 * Decodes a Radiance (`.hdr`) equirectangular image into linear float radiance.
	 *
	 * The result is a 2D `R32G32B32A32_SFLOAT` image, alpha 1. Radiance RGBE is already linear, so
	 * nothing is de-gamma'd -- see the gamma section of docs/envmaps.md for why applying one here
	 * would quietly destroy the HDR range.
	 *
	 * @throws std::runtime_error if the file cannot be read or is not an HDR image.
	 */
	[[nodiscard]] ImageData
	LoadRadianceHdr(const std::filesystem::path& path);

	/**
	 * Projects an equirectangular image onto a cube map with `faceSize` square faces, one mip.
	 *
	 * Longitude wraps and latitude clamps, so the poles are sampled without a seam at u = 0.
	 *
	 * @throws std::runtime_error if `equirect` is not a 2D float image, or `faceSize` is 0.
	 */
	[[nodiscard]] ImageData
	EquirectToCube(const ImageData& equirect, uint32_t faceSize);

	/**
	 * Prefilters a radiance environment cube map into the GGX split-sum chain the forward shading
	 * pass samples as `prefilterMap`.
	 *
	 * Mip 0 is roughness 0 -- a resample of the source rather than a convolution -- and each mip
	 * after it convolves the GGX lobe for roughness `mip / (mipLevels - 1)`. Samples are drawn by
	 * importance sampling that lobe and read from an internally built source mip pyramid, so the
	 * count needed per texel does not grow with roughness.
	 *
	 * The result is normalized by the sum of the sample weights, not by the sample count. Dividing
	 * by the count instead leaves the mean radiance climbing with roughness, which reads as a
	 * far-too-bright roughness-1 specular; `docs/envmaps.md` gives the check that catches it.
	 *
	 * @param source A cube map in `R32G32B32A32_SFLOAT`. Only its mip 0 is read; any chain it
	 *        carries is ignored, because a prefilter mip is a convolution and not a box reduction of
	 *        the level above it.
	 * @param desc Output geometry and sample budget.
	 * @param stats Optional timing and work counters.
	 * @return A cube map in `R32G32B32A32_SFLOAT` with `desc.mipLevels` levels.
	 * @throws std::runtime_error if `source` is not a float cube map, or `desc` is degenerate.
	 */
	[[nodiscard]] ImageData
	PrefilterRadiance(
		const ImageData&     source,
		const PrefilterDesc& desc,
		PrefilterStats*      stats = nullptr);
}
