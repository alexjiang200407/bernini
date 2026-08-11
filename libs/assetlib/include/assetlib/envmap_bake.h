#pragma once

namespace assetlib
{
	struct ImageData;

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
	 * noise to trade against, unlike prefilterRadiance.
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
	irradianceSh(const ImageData& source, uint32_t faceSize = 128);

	/**
	 * Convolves a cube map with the GGX lobe at one fixed roughness, into a single mip -- a defocus
	 * blur with a physically shaped kernel rather than a Gaussian.
	 *
	 * **Not the skybox path.** A backdrop's defocus is presentation, and burned into one mip it stops
	 * being authorable -- the sky a level viewport wants sharp no longer exists in the file. `skyChain`
	 * is what the sky is baked with; this remains for a caller that genuinely wants one fixed
	 * convolution and nothing else, and has no caller in the tree today.
	 *
	 * Because the result has no high angular frequencies left, `faceSize` can be small: storing detail
	 * the kernel has already removed costs bytes and buys nothing.
	 *
	 * @param roughness GGX roughness of the kernel. Its half-angle is roughly `atan(roughness^2)`, so
	 *        0.3 is a few degrees and 0.5 is around fourteen. 0 is a plain resample.
	 * @throws std::runtime_error if `source` is not a float cube map, or `faceSize` is 0.
	 */
	[[nodiscard]] ImageData
	blurCube(
		const ImageData& source,
		uint32_t         faceSize,
		float            roughness,
		uint32_t         samples = 256,
		uint32_t         threads = 0);

	/**
	 * The sky as a defocus chain: mip 0 is the sharp projection, and every level below it is the same
	 * environment convolved to the width its own texel subtends.
	 *
	 * The alternative -- blurCube into a single mip -- burns a presentation choice into the pixels,
	 * and it is the wrong place for one. How defocused a backdrop should be depends on what the
	 * viewport is for; a material editor wants the eye on the material where a level viewport wants
	 * the world it is building. Baked as a chain, that becomes `BSky::mipLevel`, which is a container
	 * edit rather than minutes of convolution, and reversible.
	 *
	 * Each level is band-limited to its own resolution rather than to a roughness, so the chain is
	 * also the mip chain a sampler would want anyway: nothing here aliases when minified.
	 *
	 * @param faceSize Face size of mip 0.
	 * @param mipLevels Levels in the chain. `faceSize >> (mipLevels - 1)` must be at least 1.
	 * @throws std::runtime_error if `source` is not a float cube map, or any count is 0.
	 */
	[[nodiscard]] ImageData
	skyChain(
		const ImageData& source,
		uint32_t         faceSize,
		uint32_t         mipLevels,
		uint32_t         samples = 256,
		uint32_t         threads = 0);

	/**
	 * The GGX roughness skyChain convolves `mip` at -- 0 for mip 0, which its own texel already band
	 * limits.
	 *
	 * Exposed so a caller can say what a `BSky::mipLevel` will actually look like: the shipped
	 * `--skybox-blur 0.15` is about mip 3 of a 512 chain.
	 */
	[[nodiscard]] float
	skyMipRoughness(uint32_t faceSize, uint32_t mip) noexcept;

	/**
	 * The exposure an environment should render at, from its irradiance map.
	 *
	 * An HDR environment's absolute scale is arbitrary, so exposure is a property of the maps and has
	 * to be re-derived whenever they change. AgX places scene-linear 0.18 at middle grey, so this
	 * returns `0.18 / L` for `L` the radiance an 18% grey surface reflects in the environment.
	 *
	 * @param irradiance A map from irradianceSh -- the cosine-weighted average incident radiance.
	 * @throws std::runtime_error if `irradiance` is not a float cube map.
	 */
	[[nodiscard]] float
	exposureFor(const ImageData& irradiance);

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
	loadRadianceHdr(const std::filesystem::path& path);

	/**
	 * Projects an equirectangular image onto a cube map with `faceSize` square faces, one mip.
	 *
	 * Longitude wraps and latitude clamps, so the poles are sampled without a seam at u = 0.
	 *
	 * @throws std::runtime_error if `equirect` is not a 2D float image, or `faceSize` is 0.
	 */
	[[nodiscard]] ImageData
	equirectToCube(const ImageData& equirect, uint32_t faceSize);

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
	prefilterRadiance(
		const ImageData&     source,
		const PrefilterDesc& desc,
		PrefilterStats*      stats = nullptr);
}
