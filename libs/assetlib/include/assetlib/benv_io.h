#pragma once
#include <assetlib_structs/ImageData.h>

namespace assetlib
{
	/**
	 * The image-based-lighting set for one environment: the three maps plus the exposure they were
	 * measured at.
	 *
	 * They travel together because they are halves of one thing. `prefilter` and `irradiance` are the
	 * specular and diffuse convolutions of the *same* radiance, in the same units, and a pair from
	 * different sources disagrees about how bright the world is -- quietly, because each looks
	 * plausible alone. Naming three files in config let that drift; naming one cannot.
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

	/** What produced a `.benv`, so a stale bake can be spotted without re-deriving it. */
	struct EnvironmentProvenance
	{
		uint64_t sourceHash = 0;  // of the source .hdr's bytes; 0 when unknown
		uint32_t samples    = 0;  // GGX samples per texel the prefilter used
		uint32_t mipLevels  = 0;  // of the prefilter chain
	};

	/**
	 * Writes an `EnvironmentMaps` as a single `.benv`.
	 *
	 * Each map is stored as a complete `.ktx2` blob rather than as raw texels, so `ktxinfo`,
	 * `ktx compare` and `ktx2check` still work on a chunk carved out of the file -- the tools that
	 * diagnose a bad environment map are worth more than the few bytes a bespoke layout would save.
	 * It also keeps the stored pixel format a per-chunk `vkFormat`, so a per-platform bake (RGB9E5
	 * everywhere, BC6H on desktop, ASTC-HDR on Apple GPUs) needs no container change.
	 *
	 * @throws std::runtime_error if a map is empty or the file cannot be written.
	 */
	void
	writeBenv(
		const EnvironmentMaps&       maps,
		const std::filesystem::path& path,
		const EnvironmentProvenance& provenance = {});

	/**
	 * Reads a `.benv` written by writeBenv.
	 *
	 * @param provenance Filled in when non-null.
	 * @throws std::runtime_error if the file is missing, is not a `.benv`, carries a version this
	 *         build does not know, or is missing one of the three maps.
	 */
	[[nodiscard]] EnvironmentMaps
	loadBenv(const std::filesystem::path& path, EnvironmentProvenance* provenance = nullptr);

	/** FNV-1a over a file's bytes, for EnvironmentProvenance::sourceHash. */
	[[nodiscard]] uint64_t
	hashFile(const std::filesystem::path& path);
}
