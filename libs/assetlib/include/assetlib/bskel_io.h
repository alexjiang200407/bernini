#pragma once
#include <assetlib/AssetCodec.h>
#include <assetlib_structs/magic.h>

namespace assetlib
{
	struct Skeleton;

	/** Serializes a skeleton into the versioned container. */
	[[nodiscard]] std::vector<std::byte>
	serializeSkeleton(const Skeleton& skeleton);

	/**
	 * Reconstructs a skeleton from a container byte stream, validated as it is read: an out-of-order
	 * or out-of-range parent is a malformed file, not something a caller has to re-check.
	 *
	 * @throws std::runtime_error on bad magic, a cache header this build does not read, a bake
	 *         token this build did not write, or a truncated / malformed stream,
	 *         or bones that are not topologically sorted.
	 */
	[[nodiscard]] Skeleton
	deserializeSkeleton(std::span<const std::byte> bytes);

	/**
	 * The codec for `c_SkeletonExtension` -- a cache entry. See AssetCodec.h.
	 *
	 * Declared here and defined in bskel_io.cpp, because `Deserialize` returns by value and this
	 * header only forward declares `Skeleton`.
	 */
	template <>
	struct AssetCodec<Skeleton>
	{
		static constexpr std::string_view c_Extension = c_SkeletonExtension;
		static constexpr AssetType        c_Type      = AssetType::kSkeleton;

		// The four bytes this container's file opens with.
		static constexpr uint32_t c_Magic = magic::c_BSkel;

		// The bake revision this container is written at; see AssetCodec.h. Bump to a fresh
		// random value whenever this writer's layout or meaning changes.
		static constexpr uint64_t c_BakeToken = 0x9be47d02a15c68f3ull;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const Skeleton& value);

		[[nodiscard]] static Skeleton
		Deserialize(std::span<const std::byte> bytes);
	};
}
