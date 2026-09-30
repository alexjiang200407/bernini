// bgpu_tests globs every .cpp under tests/ whatever the backend, and the pipeline's native object is
// D3D12's; Metal owners build their own.
#if defined(RENDERER_BACKEND_DX12)

// The backend header leans on its PCH for these and the wrl alias.
#	include <directx/d3d12.h>
#	include <wrl/client.h>  // IWYU pragma: keep

namespace wrl = Microsoft::WRL;

#	include "pipeline/ComputePipeline_d3d12.h"
#	include <bgpu/GpuContext.h>
#	include <bgpu/device/Device.h>
#	include <bgpu/pipeline/ComputeKernel.h>
#	include <bgpu/pipeline/ComputePipeline.h>
#	include <catch2/catch_test_macros.hpp>

// A PSO is the device's, so a second owner on the context gets the first one's object rather than
// building its own -- and keeps getting it after the first is gone, since the context holds it. Under
// GPU-based validation this is what stops every owner repaying the debug layer's patching.
TEST_CASE("Owners on one context share their pipeline states", "[device][compute]")
{
	auto contextDesc             = bgpu::GpuContextDesc();
	contextDesc.enableDebugLayer = true;
	contextDesc.strictError      = true;
	auto context                 = bgpu::CreateGpuContext(contextDesc);
	REQUIRE(context != nullptr);

	const auto pipelineStateOf = [](const bgpu::DeviceRef& device) {
		auto kernel = device->CreateComputeKernel(
			bgpu::ComputePipelineDesc()
				.SetShader(device->CreateShader("bgpu.CSEntrySquare"))
				.SetDebugName("bgpu.CSEntrySquare"));
		REQUIRE(kernel.pipeline != nullptr);
		return kernel.pipeline->As<bgpu::ComputePipeline>()->GetPipelineState();
	};

	auto first = bgpu::CreateDevice(context);
	REQUIRE(first != nullptr);
	ID3D12PipelineState* const built = pipelineStateOf(first);

	auto second = bgpu::CreateDevice(context);
	REQUIRE(second != nullptr);
	CHECK(pipelineStateOf(second) == built);

	first      = nullptr;
	auto third = bgpu::CreateDevice(context);
	REQUIRE(third != nullptr);
	CHECK(pipelineStateOf(third) == built);
}

#endif
