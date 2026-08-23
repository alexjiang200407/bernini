#pragma once
#include <assetlib/AssetCodec.h>

namespace assetlib
{
	struct BEnvLighting;

	/** Serializes a BEnvLighting -- both routes, their stamps and the exposure -- into a byte stream. */
	[[nodiscard]] std::vector<std::byte>
	serializeEnvLighting(const BEnvLighting& lighting);

	/**
	 * Reconstructs a BEnvLighting from a `.benvl` byte stream.
	 *
	 * @throws std::runtime_error on bad magic, a cache header this build does not read, a bake
	 *         token this build did not write, or a truncated / malformed stream.
	 */
	[[nodiscard]] BEnvLighting
	deserializeEnvLighting(std::span<const std::byte> bytes);

	/**
	 * Writes `lighting` to `path` as a `.benvl` file. Its texture references are paths relative to the
	 * data directory, not to this file.
	 *
	 * @throws std::runtime_error if the file cannot be written.
	 */
	void
	saveEnvLighting(const BEnvLighting& lighting, const std::filesystem::path& path);

	/**
	 * Loads a `.benvl` file previously written by saveEnvLighting.
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] BEnvLighting
	loadEnvLighting(const std::filesystem::path& path);

	/**
	 * The codec for `c_EnvLightingExtension` -- a cache entry. See AssetCodec.h.
	 *
	 * Declared here and defined in benvl_io.cpp, because `Deserialize` returns by value and this
	 * header only forward declares `BEnvLighting`.
	 */
	template <>
	struct AssetCodec<BEnvLighting>
	{
		static constexpr std::string_view c_Extension = c_EnvLightingExtension;
		static constexpr AssetType        c_Type      = AssetType::kEnvLighting;

		// The bake revision this container is written at; see AssetCodec.h. Bump to a fresh
		// random value whenever this writer's layout or meaning changes.
		static constexpr uint64_t c_BakeToken = 0xd48f19c7a35b062eull;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const BEnvLighting& value);

		[[nodiscard]] static BEnvLighting
		Deserialize(std::span<const std::byte> bytes);
	};
}
