#pragma once

namespace assetlib
{
	struct BMaterial;
	struct SourceStamp;

	/** Serializes a BMaterial (factors + texture file-path references + name) into a byte stream. */
	[[nodiscard]] std::vector<std::byte>
	serializeMaterial(const BMaterial& material);

	/**
	 * Reconstructs a BMaterial from a `.bmaterial` byte stream.
	 *
	 * @throws std::runtime_error on bad magic, unsupported version, or a truncated stream.
	 */
	[[nodiscard]] BMaterial
	deserializeMaterial(std::span<const std::byte> bytes);

	/**
	 * Writes `material` to `path` as a `.bmaterial` file. Texture references are file paths (relative
	 * to the data directory)
	 *
	 * @throws std::runtime_error if the file cannot be written.
	 */
	void
	saveMaterial(const BMaterial& material, const std::filesystem::path& path);

	/**
	 * Loads a `.bmaterial` file previously written by saveMaterial.
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] BMaterial
	loadMaterial(const std::filesystem::path& path);

	/**
	 * The size + last-write-time of `path`, as the bake records it. A file that does not exist (or
	 * cannot be stat'd) yields a zeroed stamp, which never compares equal to a real one -- so a
	 * deleted source reads as stale rather than as unchanged.
	 */
	[[nodiscard]] SourceStamp
	stampOf(const std::filesystem::path& path);

}
