#pragma once
#include <assetlib/AssetCodec.h>
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
	 * @throws std::runtime_error on bytes that are not a text document -- every chunk-era binary
	 *         form is refused whole -- a malformed document, or a value no rule accepts.
	 */
	[[nodiscard]] BEnv
	deserializeEnv(std::span<const std::byte> bytes);

	/**
	 * The codec for `c_EnvironmentExtension` -- a authored document. See AssetCodec.h.
	 *
	 * Declared here and defined in benv_io.cpp, because `Deserialize` returns by value and this
	 * header only forward declares `BEnv`.
	 */
	template <>
	struct AssetCodec<BEnv>
	{
		static constexpr std::string_view c_Extension = c_EnvironmentExtension;
		static constexpr AssetType        c_Type      = AssetType::kEnvironment;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const BEnv& value);

		[[nodiscard]] static BEnv
		Deserialize(std::span<const std::byte> bytes);
	};
}
