#include <core/file/file.h>

namespace core::file
{
	std::filesystem::path
	get_library_path()
	{
		// Everything links into one wasm module, so there is no library to locate; callers
		// resolve against the cwd, which is the Emscripten filesystem root the assets mount at.
		return {};
	}
}
