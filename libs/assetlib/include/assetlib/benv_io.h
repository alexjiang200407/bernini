#pragma once
#include <assetlib_structs/ImageData.h>

namespace assetlib
{
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

	struct BEnv;

	/** Serializes a BEnv -- its name and the two paths it composes -- into a byte stream. */
	[[nodiscard]] std::vector<std::byte>
	serializeEnv(const BEnv& env);

	/**
	 * Reconstructs a BEnv from a `.benv` byte stream.
	 *
	 * @throws std::runtime_error on bad magic, a truncated stream, or an unsupported version --
	 *         including 1, the retired blob format that embedded the maps themselves.
	 */
	[[nodiscard]] BEnv
	deserializeEnv(std::span<const std::byte> bytes);

	/**
	 * Writes `env` to `path` as a `.benv`. The paths it stores are relative to the data directory,
	 * not to this file.
	 *
	 * @throws std::runtime_error if the file cannot be written.
	 */
	void
	saveEnv(const BEnv& env, const std::filesystem::path& path);

	/**
	 * Loads a `.benv` previously written by saveEnv.
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] BEnv
	loadEnv(const std::filesystem::path& path);

}
