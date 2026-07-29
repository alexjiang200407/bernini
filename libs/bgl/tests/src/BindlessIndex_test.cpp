// Pins the invariants of the GetBindlessIndex seam that every later step of the descriptor
// migration must preserve: live resources on the shared heap never alias an index, and a
// resource's index does not move while it lives. Deliberately does not pin *which* index a
// resource gets -- that is the mapping the migration exists to change.
#include <d3d12.h>
#include <wrl/client.h>

namespace wrl = Microsoft::WRL;

#include "d3d12/resource/ResourceManager_d3d12.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
	wrl::ComPtr<ID3D12Device>
	CreateTestDevice()
	{
		wrl::ComPtr<ID3D12Device> device;
		REQUIRE(
			SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
		return device;
	}

	bgl::StructBufferDesc
	SmallBuffer(const char* name)
	{
		auto desc         = bgl::StructBufferDesc();
		desc.stride       = 16;
		desc.elementCount = 4;
		desc.debugName    = name;
		return desc;
	}

	bgl::TextureDesc
	SmallTexture(const char* name)
	{
		auto desc      = bgl::TextureDesc();
		desc.width     = 4;
		desc.height    = 4;
		desc.format    = bgl::Format::RGBA8_UNORM;
		desc.debugName = name;
		return desc;
	}
}

TEST_CASE("Live resources on the shared heap never alias a bindless index", "[descriptor]")
{
	auto device  = CreateTestDevice();
	auto manager = bgl::ResourceManager(device, bgl::ResourceManagerDesc());

	// Buffers and SRV textures share one shader-visible heap, so uniqueness must hold across
	// both kinds at once, not per kind.
	std::set<uint32_t> live;
	for (auto i = 0; i < 4; ++i)
	{
		const auto buffer = manager.CreateStructBuffer(SmallBuffer("unique buffer"));
		REQUIRE(!buffer.IsNull());
		CHECK(live.insert(manager.GetBindlessIndex(buffer)).second);

		const auto texture = manager.CreateTexture(SmallTexture("unique texture"));
		REQUIRE(!texture.IsNull());
		CHECK(live.insert(manager.GetBindlessIndex(texture)).second);
	}
}

TEST_CASE("A resource's bindless index is stable for its lifetime", "[descriptor]")
{
	auto device  = CreateTestDevice();
	auto manager = bgl::ResourceManager(device, bgl::ResourceManagerDesc());

	const auto buffer = manager.CreateStructBuffer(SmallBuffer("stable buffer"));
	REQUIRE(!buffer.IsNull());
	const auto index = manager.GetBindlessIndex(buffer);

	// Churn the pool around it: the survivor's index must not move.
	const auto doomed = manager.CreateStructBuffer(SmallBuffer("doomed buffer"));
	REQUIRE(!doomed.IsNull());
	manager.DestroyBuffer(doomed, false);  // no queues registered, so immediate free is safe
	const auto replacement = manager.CreateStructBuffer(SmallBuffer("replacement buffer"));
	REQUIRE(!replacement.IsNull());

	CHECK(manager.GetBindlessIndex(buffer) == index);
}

TEST_CASE("Live samplers never alias a bindless index", "[descriptor]")
{
	auto device  = CreateTestDevice();
	auto manager = bgl::ResourceManager(device, bgl::ResourceManagerDesc());

	std::set<uint32_t> live;
	for (auto i = 0; i < 4; ++i)
	{
		const auto sampler = manager.CreateSampler(bgl::SamplerDesc());
		REQUIRE(!sampler.IsNull());
		CHECK(live.insert(manager.GetBindlessIndex(sampler)).second);
	}
}
