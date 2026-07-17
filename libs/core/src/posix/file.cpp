#include <core/file/file.h>

#include <dlfcn.h>

namespace core::file
{
	std::filesystem::path
	getLibraryPath()
	{
		Dl_info info = {};

		// Resolves to the image core was linked into, not the executable.
		if (dladdr(reinterpret_cast<const void*>(&getLibraryPath), &info) == 0)
			return {};

		if (info.dli_fname == nullptr)
			return {};

		return std::filesystem::path{ info.dli_fname };
	}
}
