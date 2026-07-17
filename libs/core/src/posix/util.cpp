#include <core/platform/util.h>

#include <mach-o/dyld.h>

namespace core
{
	std::string
	get_executable_name() noexcept
	{
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);

		std::string buffer(size, '\0');
		if (_NSGetExecutablePath(buffer.data(), &size) != 0)
			return {};

		return std::filesystem::path(buffer.c_str()).stem().string();
	}
}
