#include "device/Device_wgpu.h"

#include <bgl/IGraphics.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

TEST_CASE("A WebGPU device can be acquired and reports its adapter", "[wgpu][device]")
{
	auto device = wgpu::Device(wgpu::DeviceDesc{});

	SECTION("the device and its queue are live")
	{
		REQUIRE(device.GetHandle() != nullptr);
		REQUIRE(device.GetQueue() != nullptr);
	}

	SECTION("the adapter identifies itself and its backend")
	{
		const auto& info = device.GetAdapterInfo();

		// A conforming adapter always names a concrete backend; leaving it Undefined means the
		// info struct was never populated, which a non-empty description would not reveal.
		REQUIRE(info.backendType != WGPUBackendType_Undefined);
		REQUIRE_FALSE(info.device.empty());
	}
}

TEST_CASE("Two WebGPU devices can be alive at once", "[wgpu][device]")
{
	auto first  = wgpu::Device(wgpu::DeviceDesc{});
	auto second = wgpu::Device(wgpu::DeviceDesc{});

	REQUIRE(first.GetHandle() != second.GetHandle());
}
