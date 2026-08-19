#pragma once

namespace assetlib
{
	struct BEnvLighting;

	/** Serializes a BEnvLighting -- both routes, their stamps and the exposure -- into a byte stream. */
	[[nodiscard]] std::vector<std::byte>
	serializeEnvLighting(const BEnvLighting& lighting);

	/**
	 * Reconstructs a BEnvLighting from a `.benvl` byte stream.
	 *
	 * @throws std::runtime_error on bad magic, a version newer than this build, a file from before the
	 *         schema chunk, a field no rule converts, or a truncated stream.
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

}
