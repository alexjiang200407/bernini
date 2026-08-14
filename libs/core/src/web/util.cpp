#include <core/platform/util.h>

namespace core
{
	std::string
	get_executable_name() noexcept
	{
		// A wasm module has no executable path; argv[0] is whatever the host JS passed.
		return "bernini";
	}

	// The emscripten filesystem is a memory image with no device behind it, so there is nothing to
	// flush and nothing that could have failed.
	bool
	sync_file(const std::filesystem::path&) noexcept
	{
		return true;
	}

	bool
	sync_directory(const std::filesystem::path&) noexcept
	{
		return true;
	}
}
