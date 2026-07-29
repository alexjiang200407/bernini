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

TEST_CASE("A deferred-destroyed descriptor returns only after the sweep", "[descriptor]")
{
	auto device  = CreateTestDevice();
	auto manager = bgl::ResourceManager(device, bgl::ResourceManagerDesc());

	const auto doomed = manager.CreateStructBuffer(SmallBuffer("doomed"));
	REQUIRE(!doomed.IsNull());
	const auto freedIndex = manager.GetBindlessIndex(doomed);

	// No queues are registered, so the gate is trivially clear -- but reclamation still only
	// happens in the sweep. Until it runs, the descriptor must stay off the free list.
	manager.DestroyBuffer(doomed, true);
	const auto before = manager.CreateStructBuffer(SmallBuffer("before sweep"));
	REQUIRE(!before.IsNull());
	CHECK(manager.GetBindlessIndex(before) != freedIndex);

	manager.CleanupExpiredResources();
	const auto after = manager.CreateStructBuffer(SmallBuffer("after sweep"));
	REQUIRE(!after.IsNull());
	CHECK(manager.GetBindlessIndex(after) == freedIndex);
}

TEST_CASE("An RTV-only texture holds no shader-visible descriptor", "[descriptor]")
{
	auto device  = CreateTestDevice();
	auto manager = bgl::ResourceManager(device, bgl::ResourceManagerDesc());

	const auto first = manager.CreateStructBuffer(SmallBuffer("first"));
	REQUIRE(!first.IsNull());

	auto rtvDesc  = SmallTexture("render target");
	rtvDesc.usage = bgl::TextureUsageFlag::kRenderTarget;
	const auto rt = manager.CreateTexture(rtvDesc);
	REQUIRE(!rt.IsNull());
	CHECK(manager.GetBindlessIndex(rt) == 0xFFFFFFFF);

	// It must not have consumed a heap index either: the next buffer packs right after the first.
	const auto second = manager.CreateStructBuffer(SmallBuffer("second"));
	REQUIRE(!second.IsNull());
	CHECK(manager.GetBindlessIndex(second) == manager.GetBindlessIndex(first) + 1);
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
