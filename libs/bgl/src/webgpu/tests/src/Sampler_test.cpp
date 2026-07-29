#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "resource/ResourceManager_wgpu.h"

#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// Samplers on the WebGPU backend. Unlike D3D12 there is no descriptor heap, so a slot holds the
// wgpu::Sampler itself; what is asserted here is that a real object comes back, that the desc
// survives the round trip, and that the slot follows the same deferred-destruction rules as every
// other resource.

namespace
{
	struct Fixture
	{
		core::SharedRef<Device> device  = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
		ResourceManagerRef      manager = device->CreateResourceManager(ResourceManagerDesc{});
		CommandQueueRef         queue   = device->CreateCommandQueue(QueueType::kGraphics);

		Fixture() { manager->RegisterQueue(queue.Get()); }

		~Fixture()
		{
			queue->Flush();
			manager->UnregisterQueue(queue.Get());
		}
	};
}

// These are exactly the two Scene builds at construction, so they are the pair that has to work
// before anything samples a texture through the engine.
TEST_CASE("The standard samplers a scene needs create real objects", "[wgpu][sampler]")
{
	auto fixture = Fixture{};

	const auto anisoWrap = fixture.manager->CreateSampler(
		SamplerDesc().SetAllFilters(true).SetMaxAnisotropy(16.f).SetAllAddressModes(
			SamplerAddressMode::kWrap));

	const auto linearClamp = fixture.manager->CreateSampler(
		SamplerDesc().SetAllFilters(true).SetAllAddressModes(SamplerAddressMode::kClamp));

	REQUIRE(fixture.manager->ValidSamplerHandle(anisoWrap));
	REQUIRE(fixture.manager->ValidSamplerHandle(linearClamp));

	REQUIRE(fixture.manager->GetSampler(anisoWrap).GetHandle() != nullptr);
	REQUIRE(fixture.manager->GetSampler(linearClamp).GetHandle() != nullptr);

	// Two calls with different descs must not collapse onto one slot.
	REQUIRE(anisoWrap.idx != linearClamp.idx);

	const SamplerDesc& stored = fixture.manager->GetSampler(anisoWrap).GetDesc();
	CHECK(stored.maxAnisotropy == 16.f);
	CHECK(stored.addressU == SamplerAddressMode::kWrap);
	CHECK(stored.addressW == SamplerAddressMode::kWrap);
	CHECK(stored.minFilter);
}

TEST_CASE("A point sampler is as valid as a linear one", "[wgpu][sampler]")
{
	auto fixture = Fixture{};

	const auto point = fixture.manager->CreateSampler(
		SamplerDesc().SetAllFilters(false).SetAllAddressModes(SamplerAddressMode::kMirror));

	REQUIRE(fixture.manager->ValidSamplerHandle(point));
	REQUIRE(fixture.manager->GetSampler(point).GetHandle() != nullptr);
	CHECK_FALSE(fixture.manager->GetSampler(point).GetDesc().magFilter);
}

TEST_CASE("Destroying a sampler stales its handle", "[wgpu][sampler]")
{
	auto fixture = Fixture{};

	SECTION("deferred")
	{
		const auto sampler = fixture.manager->CreateSampler(SamplerDesc());
		REQUIRE(fixture.manager->ValidSamplerHandle(sampler));

		// The slot is not reclaimed until the queue passes, but the handle stales now, so a stale
		// copy cannot address whatever takes the slot next.
		fixture.manager->DestroySampler(sampler, true);
		REQUIRE_FALSE(fixture.manager->ValidSamplerHandle(sampler));

		fixture.queue->Flush();
		fixture.manager->CleanupExpiredResources();

		REQUIRE_FALSE(fixture.manager->ValidSamplerHandle(sampler));
	}

	SECTION("immediate")
	{
		const auto sampler = fixture.manager->CreateSampler(SamplerDesc());
		REQUIRE(fixture.manager->ValidSamplerHandle(sampler));

		fixture.manager->DestroySampler(sampler, false);
		REQUIRE_FALSE(fixture.manager->ValidSamplerHandle(sampler));

		// Destroying an already-dead handle is a no-op, not a crash.
		fixture.manager->DestroySampler(sampler, false);
	}
}

TEST_CASE("An exhausted sampler pool returns a null handle", "[wgpu][sampler]")
{
	auto device = core::SharedRef<Device>::Make(WgpuDeviceDesc{});

	auto desc        = ResourceManagerDesc{};
	desc.maxSamplers = 2;

	auto manager = device->CreateResourceManager(desc);

	REQUIRE_FALSE(manager->CreateSampler(SamplerDesc()).IsNull());
	REQUIRE_FALSE(manager->CreateSampler(SamplerDesc()).IsNull());
	REQUIRE(manager->CreateSampler(SamplerDesc()).IsNull());
}
