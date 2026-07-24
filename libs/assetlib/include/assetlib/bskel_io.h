#pragma once
#include <assetlib_structs/Skeleton.h>

namespace assetlib
{
	/** Serializes a skeleton into the versioned container. */
	[[nodiscard]] std::vector<std::byte>
	serializeSkeleton(const Skeleton& skeleton);

	/**
	 * Reconstructs a skeleton from a container byte stream, validated as it is read: an out-of-order
	 * or out-of-range parent is a malformed file, not something a caller has to re-check.
	 *
	 * @throws std::runtime_error on bad magic, unsupported version, a truncated / malformed stream,
	 *         or bones that are not topologically sorted.
	 */
	[[nodiscard]] Skeleton
	deserializeSkeleton(std::span<const std::byte> bytes);

	/**
	 * @throws std::runtime_error if the file cannot be written, naming the OS's reason.
	 */
	void
	saveSkeleton(const Skeleton& skeleton, const std::filesystem::path& path);

	/** @throws std::runtime_error if the file cannot be read or is malformed. */
	[[nodiscard]] Skeleton
	loadSkeleton(const std::filesystem::path& path);
}
