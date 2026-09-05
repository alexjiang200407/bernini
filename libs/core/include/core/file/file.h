#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace core::file
{
	class IFileSystem;

	std::vector<std::byte>
	read_file_bytes(const std::string& filePath);

	std::vector<std::byte>
	read_file_bytes(std::string_view filePath);

	/** The host-path form, so a caller holding one does not spell the conversion at every call. */
	std::vector<std::byte>
	read_file_bytes(const std::filesystem::path& filePath);

	/**
	 * FNV-1a over a file's contents, read in fixed-size chunks so a file larger than memory still
	 * hashes. Equal to hash_bytes over the whole file read at once.
	 *
	 * @return Nullopt if the file cannot be opened, or if a read fails partway through it.
	 */
	[[nodiscard]] std::optional<uint64_t>
	hash_file(const std::filesystem::path& filePath);

	/**
	 * The same hash of the same bytes, read through a mount rather than off the host filesystem, in
	 * the same fixed-size chunks -- so an entry in an archive larger than memory still hashes, and a
	 * file hashes to the same value loose or packed.
	 *
	 * @param path A mount key: data-root-relative, `/`-separated, already normalized.
	 * @return Nullopt if the mount does not carry `path`, or if a read fails partway through it.
	 */
	[[nodiscard]] std::optional<uint64_t>
	hash_file(const IFileSystem& fileSystem, std::string_view path);

	std::filesystem::path
	get_library_path();

	// The running executable's own path. Unlike get_library_path this does not move with the
	// binary that core was linked into, which on a build staging libraries and executables into
	// separate directories is a different place entirely.
	std::filesystem::path
	get_executable_path();

	/**
	 * Writes `bytes` to `path` via a sibling temp file, flushed to the device and then renamed, so a
	 * crash mid-write leaves either the previous contents or the new ones and never a truncated file
	 * that still looks readable. The flush is what extends that from a process crash to a power
	 * loss: closing a stream only hands the bytes to the OS cache.
	 *
	 * The temp name carries the process id and a counter, because several processes may write to
	 * one directory at once.
	 *
	 * @throws std::runtime_error naming the OS's reason if the temp cannot be written or the rename
	 *         cannot be committed. Unlike the shader cache's equivalent this does not degrade to a
	 *         warning: its caller writes a shipped artifact, where a silent failure is discovered by
	 *         whoever loads half a project.
	 */
	void
	write_atomic(const std::filesystem::path& path, std::span<const std::byte> bytes);

	/**
	 * Renames a finished temp file onto its final name, which is the step that publishes it.
	 *
	 * Windows refuses to replace a file while any handle is still open on it, so two writers
	 * committing one derived path collide there and the loser is told access is denied for a
	 * condition that clears in milliseconds. This retries for as long as that is worth waiting,
	 * which POSIX never needs because its rename replaces silently.
	 *
	 * Callers that write their bytes themselves -- a codec handed a filename, an archive built in
	 * place -- use this rather than restating the rule, so there is one description of what
	 * committing means.
	 *
	 * @return The error that outlasted the retries, or a clear code on success. The temp file is
	 *         left where it is either way; removing it belongs to the caller that named it.
	 */
	[[nodiscard]]
	std::error_code
	commit_atomic(const std::filesystem::path& tmp, const std::filesystem::path& path);

	/** The text form: what a caller holding a serialized document writes without an as_bytes dance. */
	inline void
	write_atomic(const std::filesystem::path& path, std::string_view text)
	{
		write_atomic(path, std::as_bytes(std::span(text.data(), text.size())));
	}
}
