#include <core/platform/util.h>

namespace core
{
	std::filesystem::path
	expand_home(const std::string_view path)
	{
		if (!path.starts_with("~/") && path != "~")
			return std::filesystem::path(path);

		const char* home = std::getenv("HOME");
		if (home == nullptr)
			home = std::getenv("USERPROFILE");
		if (home == nullptr)
			return std::filesystem::path(path);

		return std::filesystem::path(home) / path.substr(path.size() > 1 ? 2 : 1);
	}
}
