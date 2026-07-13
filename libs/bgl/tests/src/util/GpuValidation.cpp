#include "util/GpuValidation.h"

namespace bgl::test
{
	namespace
	{
		// Written once by main() before the first test runs, and only read after that, so the tests need
		// no synchronisation around it.
		bool g_GpuValidation = false;
	}

	bool
	GpuValidationEnabled() noexcept
	{
		return g_GpuValidation;
	}

	void
	SetGpuValidation(bool enabled) noexcept
	{
		g_GpuValidation = enabled;
	}
}
