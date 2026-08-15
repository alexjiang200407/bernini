#pragma once

namespace assetlib
{
	/**
	 * The one form every reference path is keyed and stored in, so that the two sides of a reference
	 * -- one written by a bake, one clicked in a file browser -- meet. Identity in this project is the
	 * data-root-relative path, and `Textures/x.ktx2` and `./Meshes/../Textures/x.ktx2` are one asset.
	 */
	// assetlib::normalizePath (vat_bake.h) is this function's public alias -- one body between them.
	[[nodiscard]] inline std::string
	normalizeRef(std::string_view path)
	{
		return std::filesystem::path(path).lexically_normal().generic_string();
	}

	/**
	 * The lower-cased extension of a mount key, `.` included, or empty when it has none.
	 *
	 * On the key itself rather than through `std::filesystem::path`: a key is `/`-separated by
	 * contract, and routing one through a path to read its extension is the conversion STYLE.md's
	 * Paths section warns turns a key into something an archive lookup misses on Windows.
	 */
	[[nodiscard]] inline std::string
	extensionOf(std::string_view key)
	{
		const size_t slash = key.find_last_of('/');
		const size_t dot   = key.find_last_of('.');
		if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
			return {};

		// A leading dot names the file, it does not extend it: `.gitignore` has no extension, which
		// is what std::filesystem::path::extension answers too.
		if (dot == 0 || (slash != std::string_view::npos && dot == slash + 1))
			return {};

		std::string ext(key.substr(dot));
		std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return ext;
	}

	/** Whether `path` lies beneath `directory`. Both normalized, and neither is inside itself. */
	[[nodiscard]] inline bool
	isUnder(std::string_view path, std::string_view directory) noexcept
	{
		return path.size() > directory.size() + 1 && path.starts_with(directory) &&
		       path[directory.size()] == '/';
	}

	/**
	 * Throws unless `normalized` (a normalizeRef result) names something strictly inside the data
	 * root: the root itself, anything above it, and any rooted path -- `operator/` replaces the root
	 * for an absolute rhs, and for a drive-relative `C:foo` on another drive as well -- are all
	 * somebody else's files. `who` prefixes the message.
	 */
	inline void
	requireInsideDataRoot(std::string_view who, std::string_view normalized)
	{
		const std::filesystem::path path(normalized);

		if (normalized.empty() || normalized == "." || normalized == ".." ||
		    normalized.starts_with("../") || normalized.starts_with('/') || path.is_absolute() ||
		    path.has_root_name())
			throw std::runtime_error(
				std::string(who) + ": '" + std::string(normalized) +
				"' does not name something inside the data root");
	}
}
