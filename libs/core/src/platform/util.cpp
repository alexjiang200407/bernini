#include <core/platform/util.h>

namespace core
{
	// _dupenv_s is MSVC's replacement for the deprecated getenv, and hands back an allocation the
	// caller frees.
	std::optional<std::string>
	env_var(const char* name)
	{
#if defined(_WIN32)
		char*         value = nullptr;
		size_t        size  = 0;
		const errno_t err   = _dupenv_s(&value, &size, name);
		if (err != 0 || value == nullptr)
			return std::nullopt;

		auto out = std::string(value);
		std::free(value);
		return out;
#else
		const char* value = std::getenv(name);
		if (value == nullptr)
			return std::nullopt;

		return std::string(value);
#endif
	}

	std::filesystem::path
	expand_home(const std::string_view path)
	{
		if (!path.starts_with("~/") && path != "~")
			return std::filesystem::path(path);

		std::optional<std::string> home = env_var("HOME");
		if (!home)
			home = env_var("USERPROFILE");
		if (!home)
			return std::filesystem::path(path);

		return std::filesystem::path(*home) / path.substr(path.size() > 1 ? 2 : 1);
	}
}
