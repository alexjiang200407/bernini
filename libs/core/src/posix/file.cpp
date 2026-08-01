#include <core/file/file.h>

#include <dlfcn.h>
#include <mach-o/dyld.h>

namespace core::file
{
	std::filesystem::path
	get_library_path()
	{
		Dl_info info = {};

		// Resolves to the image core was linked into, not the executable.
		if (dladdr(reinterpret_cast<const void*>(&get_library_path), &info) == 0)
			return {};

		if (info.dli_fname == nullptr)
			return {};

		return std::filesystem::path{ info.dli_fname };
	}

	std::filesystem::path
	get_executable_path()
	{
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);

		std::string buffer(size, '\0');
		if (_NSGetExecutablePath(buffer.data(), &size) != 0)
			return {};

		return std::filesystem::path{ buffer.c_str() };
	}
}
