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

	/**
	 * Whether `material`'s baked triplet no longer reflects the source textures its routes name.
	 * `dataRoot` is the project's Data directory: every texture path a material stores is relative to
	 * it, not to the material file.
	 *
	 * This is also what decides how the material draws: a current bake is sampled as the optimized
	 * triplet, a stale one falls back to the routes it was baked from. There is no stored mode -- a
	 * material cannot claim a triplet it does not have.
	 *
	 * True when a routed source has changed, gone missing, or was never stamped (i.e. the material
	 * has routes but has never been baked), or when a map the triplet names is no longer on disk.
	 * False for a material with no routes at all -- an imported triplet-only material has no sources
	 * to be stale against, and no routes to fall back to -- and false when every routed source still
	 * measures exactly as it did at bake time and every baked map is present.
	 */
	[[nodiscard]] bool
	bakeIsStale(const BMaterial& material, const std::filesystem::path& dataRoot);
}
