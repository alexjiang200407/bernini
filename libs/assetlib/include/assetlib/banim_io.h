#pragma once
#include <assetlib/AssetCodec.h>

namespace assetlib
{
	struct AnimationSet;
	struct Skeleton;

	/** Serializes a clip set into the versioned container. */
	[[nodiscard]] std::vector<std::byte>
	serializeAnimations(const AnimationSet& animations);

	/**
	 * Reconstructs a clip set from a container byte stream, validated as it is read.
	 *
	 * @throws std::runtime_error on bad magic, a cache header this build does not read, a bake
	 *         token this build did not write, or a truncated / malformed stream,
	 *         or a clip whose samples fall outside the pool.
	 */
	[[nodiscard]] AnimationSet
	deserializeAnimations(std::span<const std::byte> bytes);

	/**
	 * @throws std::runtime_error if the file cannot be written, naming the OS's reason.
	 */
	void
	saveAnimations(const AnimationSet& animations, const std::filesystem::path& path);

	/** @throws std::runtime_error if the file cannot be read or is malformed. */
	[[nodiscard]] AnimationSet
	loadAnimations(const std::filesystem::path& path);

	/**
	 * The skeleton path `path` names, read without its samples: the header, the chunk table and the
	 * reference chunk alone. A whole-project reference scan comes through here -- the samples are
	 * megabytes and the path is a few dozen bytes.
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] std::string
	loadAnimationSkeletonPath(const std::filesystem::path& path);

	/**
	 * Whether `animations` was resampled against `skeleton`, by signature. A mismatch means the rig
	 * has had a bone inserted, removed or reordered since the clips were cooked, and their joint
	 * indices now name different bones.
	 */
	[[nodiscard]] bool
	animationsMatchSkeleton(const AnimationSet& animations, const Skeleton& skeleton) noexcept;

	/**
	 * The codec for `c_AnimationExtension` -- a cache entry. See AssetCodec.h.
	 *
	 * Declared here and defined in banim_io.cpp, because `Deserialize` returns by value and this
	 * header only forward declares `AnimationSet`.
	 */
	template <>
	struct AssetCodec<AnimationSet>
	{
		static constexpr std::string_view c_Extension = c_AnimationExtension;
		static constexpr AssetType        c_Type      = AssetType::kAnimation;

		// The bake revision this container is written at; see AssetCodec.h. Bump to a fresh
		// random value whenever this writer's layout or meaning changes.
		static constexpr uint64_t c_BakeToken = 0x41f8b6d95e07c2aaull;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const AnimationSet& value);

		[[nodiscard]] static AnimationSet
		Deserialize(std::span<const std::byte> bytes);
	};
}
