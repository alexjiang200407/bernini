#include <core/platform/util.h>

namespace core
{
	std::string
	get_executable_name() noexcept
	{
		// A wasm module has no executable path; argv[0] is whatever the host JS passed.
		return "bernini";
	}
}
