#pragma once
#include <assetlib/cancel.h>
#include <assetlib/project_layout.h>
#include <assetlib_structs/ImageData.h>

namespace assetlib
{
	/**
	 * One HDRI becoming an environment, end to end: decode it, project it onto a cube, convolve the
	 * two lighting maps and the sky's defocus chain out of it, and write the three containers a
	 * project stores.
	 *
	 * The stages are separate functions rather than one call because they are separately useful --
	 * a sky can be re-authored without paying for the lighting's convolution -- and they are one
	 * header because a caller reading any of them needs to know which stage feeds it. See
	 * docs/envmaps.md for the authoring traps each stage has.
	 */

	// --- Decode the source and project it onto a cube ------------------------------------------

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

	// --- Convolve ------------------------------------------------------------------------------

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

	// --- What a renderer consumes --------------------------------------------------------------

	/**
	 * The image-based-lighting set for one environment, decoded: the three maps plus the exposure
	 * they were measured at. What resolveEnvironment returns and a renderer consumes.
	 *
	 * They travel together because they are halves of one thing. `prefilter` and `irradiance` are the
	 * specular and diffuse convolutions of the *same* radiance, in the same units, and a pair from
	 * different sources disagrees about how bright the world is -- quietly, because each looks
	 * plausible alone.
	 *
	 * `brdf_lut` is deliberately absent. It is the split-sum BRDF integral, a property of the shading
	 * model rather than of any environment, so embedding it would duplicate it per environment and
	 * re-couple what is meant to be shared.
	 */
	struct EnvironmentMaps
	{
		ImageData prefilter;   // GGX split-sum chain, one mip per roughness
		ImageData irradiance;  // clamped-cosine convolution, one mip
		ImageData skybox;      // the environment itself, unfiltered

		/**
		 * The exposure this environment renders at. An HDR environment's absolute scale is arbitrary,
		 * so this is a property of the maps and not of the scene, and it must change whenever they do
		 * -- which is why it lives in the file rather than in config.
		 */
		float exposure = 1.0f;

		// Move-only, following ImageData: the maps are megabytes each and nothing wants a silent copy.
		EnvironmentMaps()                           = default;
		EnvironmentMaps(EnvironmentMaps&&) noexcept = default;
		EnvironmentMaps(const EnvironmentMaps&)     = delete;
		EnvironmentMaps&
		operator=(EnvironmentMaps&&) noexcept = default;
		EnvironmentMaps&
		operator=(const EnvironmentMaps&) = delete;
	};

	/**
	 * A `.benv` followed to its pixels: the decoded maps of the sky and lighting it references,
	 * plus the sky's presentation. What a renderer consumes, where BEnv is what a project stores.
	 */
	struct ResolvedEnvironment
	{
		// Pieces the .benv does not reference stay empty; loading half an environment is the
		// caller's decision to make, not an error.
		EnvironmentMaps maps;

		uint32_t skyMipLevel  = 0;
		float    skyRotationY = 0.0f;

		// Move-only, following EnvironmentMaps: the maps are megabytes each.
		ResolvedEnvironment()                               = default;
		ResolvedEnvironment(ResolvedEnvironment&&) noexcept = default;
		ResolvedEnvironment(const ResolvedEnvironment&)     = delete;
		ResolvedEnvironment&
		operator=(ResolvedEnvironment&&) noexcept = default;
		ResolvedEnvironment&
		operator=(const ResolvedEnvironment&) = delete;
	};

	// --- Import one HDRI into a project --------------------------------------------------------

	/**
	 * One HDRI, imported into a project as the environment family.
	 *
	 * The three outputs are separable because they are separately useful: a sky can be re-authored
	 * without paying for the lighting's convolution, and an existing `.bsky` can be given a `.benvl`
	 * later. Every path in the result is relative to `dataRoot`.
	 */
	struct EnvImportDesc
	{
		std::filesystem::path dataRoot;  // the project's Data directory
		std::filesystem::path source;    // an equirectangular `.hdr`, or a cube map `.ktx2`

		// Names every file the import writes: `Sky/<name>.bsky`, `textures_src/<name>_sky.ktx2`, ...
		std::string name = "env";

		/**
		 * Where each part lands, relative to the data root. Defaulted to the project's categories, so
		 * a caller that does not care writes the layout `Project::Create` scaffolds.
		 *
		 * A caller that does care -- the import dialog offers a folder per part -- names a
		 * subdirectory *inside* the category rather than replacing it, so the categories stay the
		 * layout every other reference is written against.
		 */
		std::filesystem::path skyDir         = c_SkyDirectoryName;
		std::filesystem::path lightingDir    = c_EnvLightingDirectoryName;
		std::filesystem::path environmentDir = c_EnvironmentsDirectoryName;
		std::filesystem::path sourceDir      = c_TexturesSrcDirectoryName;

		bool sky         = true;  // write the `.bsky`
		bool lighting    = true;  // write the `.benvl` -- the prefilter/irradiance pair
		bool environment = true;  // write the `.benv` composing whichever of the two were written

		uint32_t skyFaceSize = 512;

		// Levels in the sky's defocus chain -- see skyChain. The backdrop is always baked sharp at
		// mip 0; how defocused it is drawn is `skyMipLevel`, which a viewer may overrule.
		uint32_t skyMips = 6;

		// Which level the written `.benv` document presents. 0 is the sharp projection.
		uint32_t skyMipLevel = 0;

		uint32_t prefilterFaceSize  = 256;
		uint32_t prefilterMips      = 7;  // must match the shader's MAX_REFLECTION_LOD + 1
		uint32_t prefilterSamples   = 128;
		uint32_t irradianceFaceSize = 128;

		uint32_t threads = 0;  // 0 means hardware concurrency
	};

	/** What an import wrote, so a caller can name it, open it, or report it. */
	struct EnvImportResult
	{
		std::string sky;  // empty when that output was not requested
		std::string lighting;
		std::string environment;

		/**
		 * Every file this call brought into being, data-root relative -- not the ones it overwrote,
		 * and not the baked maps (see importEnvironment).
		 */
		std::vector<std::string> written;

		float exposure = 1.0f;  // what the lighting derived, or 1 when none was written
	};

	/**
	 * Imports `desc.source` into `desc.dataRoot` as a `.bsky`, a `.benvl` and the `.benv` composing
	 * them, writing the float intermediates into `textures_src/` as the routed sources and baking each
	 * into `Textures/`.
	 *
	 * **Rolls back on failure.** A cancelled or failed import removes the files it created, so a
	 * half-written environment is never left behind. It removes only what it *created*: a file that was
	 * already there is one this import overwrote rather than made, and destroying it would take the
	 * previous import's work with it.
	 *
	 * **Baked maps are deliberately not rolled back.** They are content-addressed and shared, so the
	 * map this import wrote may be the same file another environment already names -- and deleting one
	 * would take it out from under that. An orphan left by a failed import is exactly what
	 * `findUnusedBakedTextures` sweeps, which is the mechanism that already owns this question.
	 *
	 * @param cancel Polled between the projection, each convolution and each bake -- the four steps
	 *        worth interrupting. A cancelled import rolls back like a failed one.
	 * @throws std::runtime_error if nothing is selected, if `dataRoot` is not a directory, or if the
	 *         source cannot be read.
	 * @throws Cancelled if `cancel` is signalled.
	 */
	[[nodiscard]] EnvImportResult
	importEnvironment(const EnvImportDesc& desc, const CancelToken& cancel = {});

	/**
	 * Every file `desc` would write, data-root relative, without writing any of them.
	 *
	 * For a caller that must decide *before* importing whether it would land on something already
	 * there -- the editor refuses rather than overwrites, and cannot ask that question by trying it.
	 * Naming the files here rather than in the caller is what keeps the two from disagreeing about
	 * where an import goes.
	 *
	 * The baked maps are not included: they are content-addressed, so a collision with one is two
	 * imports agreeing on content rather than one destroying the other.
	 */
	[[nodiscard]] std::vector<std::string>
	environmentImportTargets(const EnvImportDesc& desc);

	// --- What the texture prune reads ----------------------------------------------------------

	/**
	 * Whether `fileName` is a name AssetStore::BakeSky or BakeEnvLighting could have written:
	 * `<group>_<16 hex digits>.ktx2` for an environment group. The counterpart of isBakedMapName,
	 * disjoint from it by group name, and what lets the texture prune consider environment maps
	 * without mistaking them for a material's.
	 */
	[[nodiscard]] bool
	isBakedEnvMapName(std::string_view fileName) noexcept;
}
