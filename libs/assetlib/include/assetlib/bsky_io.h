#pragma once
#include <core/file/IFileSystem.h>

namespace assetlib
{
	struct BSky;

	/** Serializes a BSky -- its route, its stamp and its presentation -- into a byte stream. */
	[[nodiscard]] std::vector<std::byte>
	serializeSky(const BSky& sky);

	/**
	 * Reconstructs a BSky from a `.bsky` byte stream.
	 *
	 * @throws std::runtime_error on bad magic, unsupported version, or a truncated stream.
	 */
	[[nodiscard]] BSky
	deserializeSky(std::span<const std::byte> bytes);

	/**
	 * Writes `sky` to `path` as a `.bsky` file. Its texture references are paths relative to the data
	 * directory, not to this file.
	 *
	 * @throws std::runtime_error if the file cannot be written.
	 */
	void
	saveSky(const BSky& sky, const std::filesystem::path& path);

	/**
	 * Loads a `.bsky` file previously written by saveSky.
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] BSky
	loadSky(const std::filesystem::path& path);

	/**
	 * The mounted overload: `path` is data-root-relative and resolved through `fileSystem`, so the
	 * container may equally be a loose file or an entry in an archive.
	 *
	 * @throws std::runtime_error if the container is absent, cannot be read, or is malformed.
	 */
	[[nodiscard]] BSky
	loadSky(const core::file::IFileSystem& fileSystem, std::string_view path);
}
