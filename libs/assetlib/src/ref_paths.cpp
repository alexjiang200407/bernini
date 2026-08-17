#include "ref_paths.h"

#include <core/err/util.h>

namespace assetlib
{
	std::string
	normalizeRef(std::string_view path)
	{
		return std::filesystem::path(path).lexically_normal().generic_string();
	}

	std::string
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

	bool
	isUnder(std::string_view path, std::string_view directory) noexcept
	{
		return path.size() > directory.size() + 1 && path.starts_with(directory) &&
		       path[directory.size()] == '/';
	}

	void
	requireInsideDataRoot(std::string_view who, std::string_view normalized)
	{
		const std::filesystem::path path(normalized);

		core::throw_runtime_error_if(
			normalized.empty() || normalized == "." || normalized == ".." ||
				normalized.starts_with("../") || normalized.starts_with('/') ||
				path.is_absolute() || path.has_root_name(),
			"{}: '{}' does not name something inside the data root",
			who,
			normalized);
	}
}
