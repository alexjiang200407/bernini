#include "util/GpuValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <core/platform/util.h>

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

	bool
	GpuValidationActive() noexcept
	{
		return g_GpuValidation || core::env_var("MTL_SHADER_VALIDATION").has_value() ||
		       core::env_var("METAL_DEVICE_WRAPPER_TYPE").has_value();
	}

	void
	SkipUnderGpuValidation()
	{
		if (GpuValidationActive())
			SKIP("a measurement sweep; its passes are validated by a kept case in this file");
	}
}
