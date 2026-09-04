#include <common/TracySystem.hpp>
#include <core/profiling/thread_name.h>

#include <tracy/Tracy.hpp>

namespace core::profiling
{
	void
	name_this_thread([[maybe_unused]] const char* const name) noexcept
	{
#ifdef TRACY_ENABLE
		static thread_local bool g_Named = false;
		if (g_Named)
			return;

		g_Named = true;
		tracy::SetThreadName(name);
#endif
	}
}
