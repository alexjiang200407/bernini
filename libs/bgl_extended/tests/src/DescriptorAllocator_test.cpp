// Exercises the allocator against a real device but never wires it into a frame: these pin the
// index-management contract the descriptor migration (D4 of the plan) will rely on.
//
// bgl_extended_tests globs every .cpp under tests/ whatever the backend, so a D3D12-only case has to exclude
// itself: the headers below do not exist on a Metal build.
#if defined(RENDERER_BACKEND_DX12)

#	include <d3d12.h>
#	include <wrl/client.h>

namespace wrl = Microsoft::WRL;

#	include "d3d12/resource/DescriptorAllocator_d3d12.h"

#	include <catch2/catch_test_macros.hpp>

namespace
{
	constexpr uint32_t c_Capacity = 8;

	wrl::ComPtr<ID3D12Device>
	CreateTestDevice()
	{
		wrl::ComPtr<ID3D12Device> device;
		REQUIRE(
			SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
		return device;
	}

	bgl::DescriptorAllocator
	MakeAllocator(ID3D12Device* device)
	{
		return bgl::DescriptorAllocator(
			device,
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
			c_Capacity,
			D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
	}
}

TEST_CASE("Descriptor indices are unique until the heap is exhausted", "[descriptor]")
{
	auto device    = CreateTestDevice();
	auto allocator = MakeAllocator(device.Get());

	std::set<uint32_t> live;
	for (uint32_t i = 0; i < c_Capacity; ++i)
	{
		const auto index = allocator.Allocate();
		CHECK(index < c_Capacity);
		CHECK(live.insert(index).second);  // an index handed out twice fails to insert
	}

	CHECK_THROWS_AS(allocator.Allocate(), std::runtime_error);
}

TEST_CASE("Only a freed index is handed out again", "[descriptor]")
{
	auto device    = CreateTestDevice();
	auto allocator = MakeAllocator(device.Get());

	for (uint32_t i = 0; i < c_Capacity; ++i)
	{
		(void)allocator.Allocate();
	}

	// Free a subset of a full heap: exactly those indices must come back, and only once each.
	const auto freed = std::set<uint32_t>{ 2, 5 };
	for (const auto index : freed)
	{
		allocator.Free(index);
	}

	std::set<uint32_t> reissued;
	for (size_t i = 0; i < freed.size(); ++i)
	{
		CHECK(reissued.insert(allocator.Allocate()).second);
	}
	CHECK(reissued == freed);

	CHECK_THROWS_AS(allocator.Allocate(), std::runtime_error);
}

TEST_CASE("The heap can be drained and refilled to capacity", "[descriptor]")
{
	auto device    = CreateTestDevice();
	auto allocator = MakeAllocator(device.Get());

	for (uint32_t i = 0; i < c_Capacity; ++i)
	{
		(void)allocator.Allocate();
	}
	for (uint32_t i = 0; i < c_Capacity; ++i)
	{
		allocator.Free(i);
	}

	std::set<uint32_t> live;
	for (uint32_t i = 0; i < c_Capacity; ++i)
	{
		CHECK(live.insert(allocator.Allocate()).second);
	}

	CHECK_THROWS_AS(allocator.Allocate(), std::runtime_error);
}

TEST_CASE("Descriptor addresses step by the device increment", "[descriptor]")
{
	auto device    = CreateTestDevice();
	auto allocator = MakeAllocator(device.Get());

	const auto start = allocator.GetD3D12Heap()->GetCPUDescriptorHandleForHeapStart();
	const auto increment =
		device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	CHECK(allocator.GetCapacity() == c_Capacity);
	for (uint32_t i = 0; i < c_Capacity; ++i)
	{
		CHECK(allocator.GetCpuHandle(i).ptr == start.ptr + static_cast<SIZE_T>(i) * increment);
	}
}

#endif
