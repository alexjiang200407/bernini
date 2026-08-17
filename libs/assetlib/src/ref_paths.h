#pragma once

namespace assetlib
{
	/**
	 * The one form every reference path is keyed and stored in, so the two sides of a reference --
	 * one written by a bake, one clicked in a file browser -- meet. `Textures/x.ktx2` and
	 * `./Meshes/../Textures/x.ktx2` are one asset.
	 */
	// assetlib::normalizePath (vat_bake.h) is this function's public alias -- one body between them.
	[[nodiscard]] std::string
	normalizeRef(std::string_view path);

	/**
	 * The lower-cased extension of a mount key, `.` included, or empty when it has none.
	 *
	 * Read off the key rather than through `std::filesystem::path`: that conversion is the one
	 * STYLE.md's Paths section warns an archive lookup then misses on Windows.
	 */
	[[nodiscard]] std::string
	extensionOf(std::string_view key);

	/** Whether `path` lies beneath `directory`. Both normalized, and neither is inside itself. */
	[[nodiscard]] bool
	isUnder(std::string_view path, std::string_view directory) noexcept;

	/**
	 * @throws std::runtime_error unless `normalized` (a normalizeRef result) names something
	 *         strictly inside the data root. `who` prefixes the message.
	 */
	void
	requireInsideDataRoot(std::string_view who, std::string_view normalized);
}
