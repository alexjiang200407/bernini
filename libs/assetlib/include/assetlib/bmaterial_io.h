#pragma once
#include <core/file/IFileSystem.h>

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
	 * The mounted overload: `path` is data-root-relative and resolved through `fileSystem`, so the
	 * container may equally be a loose file or an entry in an archive.
	 *
	 * @throws std::runtime_error if the container is absent, cannot be read, or is malformed.
	 */
	[[nodiscard]] BMaterial
	loadMaterial(const core::file::IFileSystem& fileSystem, std::string_view path);

	/**
	 * The size + content hash of `path`, as the bake records it. A file that does not exist (or
	 * cannot be read) yields a zeroed stamp, which never compares equal to a real one -- so a
	 * deleted source reads as stale rather than as unchanged.
	 *
	 * Memoized for the life of the process against the file's size and mtime, so a source hashed
	 * once is re-stamped for a stat. A file rewritten in place is re-hashed, because that moves its
	 * mtime; one rewritten with its mtime forced back to the same value is not.
	 */
	[[nodiscard]] SourceStamp
	stampOf(const std::filesystem::path& path);

	/**
	 * Whether `material`'s baked triplet no longer reflects the source textures its routes name.
	 * `dataRoot` is the project's Data directory: every texture path a material stores is relative to
	 * it, not to the material file.
	 *
	 * True when a routed source has changed, gone missing, or was never stamped (i.e. the material
	 * has routes but has never been baked), or when a map the triplet names is no longer on disk.
	 * False for a material with no routes at all -- an imported triplet-only material has no sources
	 * to be stale against, and no routes to fall back to -- and false when every routed source still
	 * measures exactly as it did at bake time and every baked map is present.
	 *
	 * This is the rebake question, not the draw question: see drawsLoose.
	 */
	[[nodiscard]] bool
	bakeIsStale(const BMaterial& material, const std::filesystem::path& dataRoot);

	/**
	 * Whether `material` draws from its authoring routes rather than its baked triplet. Derived from
	 * the disk, never stored -- a material cannot claim a triplet it does not have.
	 *
	 * A stale bake falls back to the routes it was composited from, but only routes that are still
	 * there can be sampled. A material whose sources have gone is stale and unbakeable, yet loose is
	 * not a representation it can draw either, so it keeps its triplet: degraded where a map is also
	 * missing, rather than failing to open a file that does not exist.
	 */
	[[nodiscard]] bool
	drawsLoose(const BMaterial& material, const std::filesystem::path& dataRoot);
}
