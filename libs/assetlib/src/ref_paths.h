#pragma once

namespace assetlib
{
	/**
	 * The one form every reference path is keyed and stored in, so the two sides of a reference --
	 * one written by a bake, one clicked in a file browser -- meet. `Derived/BakedTextures/x.ktx2`
	 * and `./Derived/Meshes/../BakedTextures/x.ktx2` are one asset.
	 */
	// assetlib::normalizePath (codecs.h) is this function's public alias -- one body between them.
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

	/**
	 * `path` as a mount key relative to `dataRoot`: `/`-separated, as every stored reference is.
	 *
	 * Returns `path`'s own generic spelling when the two share no common root, which is the only
	 * answer that keeps a caller writing *something* rather than a `..` chain out of the project.
	 */
	[[nodiscard]] std::string
	mountKeyFor(const std::filesystem::path& dataRoot, const std::filesystem::path& path);

	/** Whether `path` lies beneath `directory`. Both normalized, and neither is inside itself. */
	[[nodiscard]] bool
	isUnder(std::string_view path, std::string_view directory) noexcept;

	// requireInsideDataRoot is declared in codecs.h, which every user of this header includes: it
	// is public, unlike the rest of these.
}
