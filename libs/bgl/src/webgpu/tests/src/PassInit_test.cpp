#include "device/Device_wgpu.h"
#include "passes/CompactInstancesPass.h"
#include "passes/ExpandMeshletsPass.h"
#include "passes/ForwardPass.h"
#include "passes/SkyboxPass.h"
#include "passes/TransparentSortPass.h"
#include "resource/ResourceManager.h"

#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// Every render pass's Init on the WebGPU device -- the exact set RenderContext runs at
// CreateGraphics, which builds every PSO the renderer will ever use. Building a pipeline is where
// Slang emits WGSL and Dawn's front-end (Tint) validates it, so this is the gate that the whole
// shader surface -- all ten forward pixel buckets through the factor-only material arms, the
// vertex-pulling geometry arm, the skybox, and the five culling and four expansion kernels --
// actually compiles on this backend. slangc accepting a module at build time is not that bar.
TEST_CASE("Every render pass initializes on the WebGPU device", "[wgpu][passes]")
{
	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});

	REQUIRE_FALSE(device->SupportsMeshShaders());

	SECTION("compact instances")
	{
		auto pass = CompactInstancesPass();
		pass.Init(device.Get(), resources);
		pass.Release(false);
	}

	SECTION("transparent sort")
	{
		auto pass = TransparentSortPass();
		pass.Init(device.Get(), resources);
		pass.Release(false);
	}

	SECTION("expand meshlets")
	{
		auto pass = ExpandMeshletsPass();
		pass.Init(device.Get(), resources);
		pass.Release(false);
	}

	SECTION("skybox")
	{
		auto pass = SkyboxPass();
		pass.Init(device.Get());
		pass.Release();
	}

	SECTION("forward")
	{
		auto pass = ForwardPass();
		pass.Init(device.Get());
		pass.Release();
	}
}
