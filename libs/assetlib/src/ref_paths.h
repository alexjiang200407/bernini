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
