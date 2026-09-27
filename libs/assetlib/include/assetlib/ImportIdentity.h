#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace assetlib
{
	enum class AssetType : uint32_t;

	struct ImportIdentity
	{
		uint64_t    id = 0;  // Zero means not yet migrated; never a new import's identity.
		std::string label;   // Source filename including extension, frozen at first import.

		bool
		operator==(const ImportIdentity&) const = default;
	};

	/** Mints a nonzero random id and freezes the source key's filename. Does not read the source. */
	[[nodiscard]] ImportIdentity
	makeImportIdentity(std::string_view sourceKey);

	/**
	 * A category key ending in <label>-<16 lowercase hex digits>.<ext>.
	 * @throws std::runtime_error for an invalid identity or a kind other than mesh, skeleton,
	 *         animation, sky or environment lighting. A bound rig is resolved from the document,
	 *         never by manufacturing an output key for it.
	 */
	[[nodiscard]] std::string
	importOutputKey(const ImportIdentity& identity, AssetType kind);

	/** The extracted-image directory under Derived/SourceTextures, with the same identity label. */
	[[nodiscard]] std::string
	importTextureDirectory(const ImportIdentity& identity);
}
