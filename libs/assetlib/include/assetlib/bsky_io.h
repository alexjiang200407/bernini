#pragma once
#include <assetlib/AssetCodec.h>
#include <assetlib_structs/magic.h>

namespace assetlib
{
	struct BSky;

	/** Serializes a BSky -- its route, its stamp and its presentation -- into a byte stream. */
	[[nodiscard]] std::vector<std::byte>
	serializeSky(const BSky& sky);

	/**
	 * Reconstructs a BSky from a `.bsky` byte stream.
	 *
	 * @throws std::runtime_error on bad magic, a cache header this build does not read, a bake
	 *         token this build did not write, or a truncated / malformed stream.
	 */
	[[nodiscard]] BSky
	deserializeSky(std::span<const std::byte> bytes);

	/**
	 * The codec for `c_SkyExtension` -- a cache entry. See AssetCodec.h.
	 *
	 * Declared here and defined in bsky_io.cpp, because `Deserialize` returns by value and this
	 * header only forward declares `BSky`.
	 */
	template <>
	struct AssetCodec<BSky>
	{
		static constexpr std::string_view c_Extension = c_SkyExtension;
		static constexpr AssetType        c_Type      = AssetType::kSky;

		// The four bytes this container's file opens with.
		static constexpr uint32_t c_Magic = magic::c_BSky;

		// The bake revision this container is written at; see AssetCodec.h. Bump to a fresh
		// random value whenever this writer's layout or meaning changes.
		static constexpr uint64_t c_BakeToken = 0x7c25e8b1904dfa36ull;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const BSky& value);

		[[nodiscard]] static BSky
		Deserialize(std::span<const std::byte> bytes);
	};
}
