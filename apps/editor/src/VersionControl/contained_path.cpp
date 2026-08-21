#include "VersionControl/contained_path.h"

namespace
{
	namespace fs = std::filesystem;

	fs::path
	Resolved(const fs::path& path)
	{
		std::error_code ec;
		const fs::path  resolved = fs::weakly_canonical(path, ec);
		return ec ? path.lexically_normal() : resolved;
	}
}

namespace editor
{
	std::optional<fs::path>
	RelativeToRoot(const fs::path& root, const fs::path& path)
	{
		if (!path.is_absolute() && path.has_root_name())
		{
			return std::nullopt;
		}

		const fs::path joined =
			path.is_absolute() ? path.lexically_normal() : (root / path).lexically_normal();

		const fs::path relative = Resolved(joined).lexically_relative(Resolved(root));
		if (relative.empty() || relative == "." || *relative.begin() == "..")
		{
			return std::nullopt;
		}
		return relative;
	}
}
