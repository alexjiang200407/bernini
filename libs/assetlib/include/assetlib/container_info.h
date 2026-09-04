#pragma once
#include <assetlib_structs/SourceRef.h>
#include <assetlib_structs/SourceStamp.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>

namespace assetlib
{
	// Declared, not included: residentBytes only names them, and BMesh.h alone drags the vertex
	// layout and the node tree into every consumer of this header.
	struct AnimationSet;
	struct BMesh;
	struct Skeleton;

	/**
	 * Whether `bytes` opens as a JSON object -- an authored text document rather than a
	 * magic-headed container. Leading whitespace is skipped, exactly as the loaders skip it, so a
	 * tool dispatching on this agrees with what the engine will read.
	 */
	[[nodiscard]] bool
	isTextAssetDocument(std::span<const std::byte> bytes) noexcept;

	/** A cache entry's identity: the token it was written at, and its source. */
	struct CacheEntryInfo
	{
		uint32_t  magic     = 0;
		uint64_t  bakeToken = 0;
		SourceRef source;
	};

	/**
	 * The cache key of any cache-entry container, or nullopt for bytes in another format -- what
	 * `describe --key` prints without loading a payload.
	 */
	[[nodiscard]] std::optional<CacheEntryInfo>
	inspectCacheEntry(std::span<const std::byte> bytes);

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
	 * What a loaded container holds in memory: its vectors' bytes and its string pool, and not the
	 * object around them.
	 *
	 * **Not a serialized size.** A container on disk is chunks a codec may have compressed, and
	 * what a memory report is asked about is what the process is holding now. The two differ by
	 * whatever the codec did, so neither substitutes for the other.
	 *
	 * Here rather than in `assetlib_structs`, which is deliberately data with no behaviour, and
	 * beside each struct rather than derived by a caller -- a caller's copy of the field list stops
	 * being right the first time a field is added.
	 */
	[[nodiscard]] uint64_t
	residentBytes(const BMesh& mesh) noexcept;

	[[nodiscard]] uint64_t
	residentBytes(const Skeleton& skeleton) noexcept;

	[[nodiscard]] uint64_t
	residentBytes(const AnimationSet& animations) noexcept;
}
