#include <bgpu/GpuContext.h>
#include <catch2/catch_test_macros.hpp>

// The clock itself is only observable in a vsync'd window (docs/bgpu.md § Maximum performance);
// these pin what each backend tells the driver, and what it reports back.

TEST_CASE("Maximum performance is off unless a client asks for it", "[device][maxperf]")
{
	CHECK_FALSE(bgpu::GpuContextDesc().preferMaximumPerformance);

	auto context = bgpu::CreateGpuContext(bgpu::GpuContextDesc());
	CHECK(context->GetMaximumPerformance() == bgpu::MaximumPerformance::kNotRequested);
}

TEST_CASE("A request for maximum performance never fails the context", "[device][maxperf]")
{
	auto desc                     = bgpu::GpuContextDesc();
	desc.preferMaximumPerformance = true;

	auto context = bgpu::CreateGpuContext(desc);
	CHECK(context->GetMaximumPerformance() != bgpu::MaximumPerformance::kNotRequested);
#if defined(__APPLE__)
	CHECK(context->GetMaximumPerformance() == bgpu::MaximumPerformance::kUnavailable);
#endif
}
