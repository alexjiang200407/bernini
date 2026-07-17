#include <core/file/file.h>

#include <dlfcn.h>

namespace core::file
{
	std::filesystem::path
	getLibraryPath()
	{
		Dl_info info = {};

		// Takes the address of this function, so it resolves to whichever image core was linked
		// into -- not to the executable.
		if (dladdr(reinterpret_cast<const void*>(&getLibraryPath), &info) == 0)
			return {};

		if (info.dli_fname == nullptr)
			return {};

		return std::filesystem::path{ info.dli_fname };
	}
}
