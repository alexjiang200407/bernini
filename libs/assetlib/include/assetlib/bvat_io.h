#pragma once
#include <assetlib/AssetCodec.h>
#include <assetlib_structs/magic.h>

namespace assetlib
{
	struct BVat;

	/**
	 * Serializes a bake's output into the versioned container, texture payloads included.
	 *
	 * @throws std::runtime_error if `vat` has no texture payloads (a tables-only read is not a
	 *         thing to write back), or if its tables disagree with its own dimensions.
	 */
	[[nodiscard]] std::vector<std::byte>
	serializeVat(const BVat& vat);

	/**
	 * Reconstructs a BVat from a container byte stream, validated as it is read.
	 *
	 * @throws std::runtime_error on bad magic, unsupported version, a truncated / malformed stream,
	 *         a missing texture chunk, or tables that disagree with the recorded dimensions.
	 */
	[[nodiscard]] BVat
	deserializeVat(std::span<const std::byte> bytes);

	/**
	 * @throws std::runtime_error if the file cannot be written, naming the OS's reason.
	 */
	void
	saveVat(const BVat& vat, const std::filesystem::path& path);

	/** @throws std::runtime_error if the file cannot be read or is malformed. */
	[[nodiscard]] BVat
	loadVat(const std::filesystem::path& path);

	/**
	 * Everything but the pixels: the header, the chunk table and the table chunks alone, leaving
	 * `positionsKtx2` / `normalsKtx2` empty. The pixel chunks are the overwhelming bulk of the
	 * file and are never read, so `describe` and any whole-project survey must come through here
	 * rather than loadVat. (The palettes still load, and they are megabytes on a long clip set --
	 * a scan that wants the references alone wants loadVatRefs.)
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] BVat
	loadVatTables(const std::filesystem::path& path);

	/** The three inputs a `.bvat` was baked from. See loadVatRefs. */
	struct VatRefs
	{
		std::string mesh;
		std::string skeleton;
		std::string animations;
	};

	/**
	 * What `path` was baked from, read seek-only like loadVatTables -- the reference scan's entry
	 * point.
	 *
	 * @throws std::runtime_error if the file cannot be read or is malformed.
	 */
	[[nodiscard]] VatRefs
	loadVatRefs(const std::filesystem::path& path);

	/**
	 * The codec for `c_VatExtension` -- a cache entry. See AssetCodec.h.
	 *
	 * Declared here and defined in bvat_io.cpp, because `Deserialize` returns by value and this
	 * header only forward declares `BVat`.
	 */
	template <>
	struct AssetCodec<BVat>
	{
		static constexpr std::string_view c_Extension = c_VatExtension;
		static constexpr AssetType        c_Type      = AssetType::kVat;

		// The four bytes this container's file opens with.
		static constexpr uint32_t c_Magic = magic::c_BVat;

		// The bake revision this container is written at; see AssetCodec.h. Bump to a fresh
		// random value whenever this writer's layout or meaning changes.
		static constexpr uint64_t c_BakeToken = 0x25b90ce8f7143ad9ull;

		[[nodiscard]] static std::vector<std::byte>
		Serialize(const BVat& value);

		[[nodiscard]] static BVat
		Deserialize(std::span<const std::byte> bytes);
	};
}
