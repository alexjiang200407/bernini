#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "resource/ResourceManager.h"

#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The RHI's data plane end to end: upload bytes through a command list, copy them into a
// readback buffer, submit, wait on the emulated fence, and map. Everything below the pixel level
// depends on this working, so it is asserted on real bytes rather than on calls not crashing.

namespace
{
	struct Fixture
	{
		core::SharedRef<Device> device  = core::SharedRef<Device>::Make(wgpu::DeviceDesc{});
		ResourceManagerRef      manager = device->CreateResourceManager(ResourceManagerDesc{});
		CommandQueueRef         queue   = device->CreateCommandQueue(QueueType::kGraphics);
		core::SharedRef<ICommandAllocator> allocator =
			device->CreateCommandAllocator(QueueType::kGraphics);
		CommandListRef list = device->CreateCommandList(CommandListDesc{}, allocator, manager);

		Fixture() { manager->RegisterQueue(queue.Get()); }

		~Fixture()
		{
			queue->Flush();
			manager->UnregisterQueue(queue.Get());
		}
	};
}

TEST_CASE("Bytes written to a buffer read back unchanged", "[wgpu][buffer]")
{
	auto fixture = Fixture{};

	auto payload = std::vector<uint32_t>(64);
	for (uint32_t i = 0; i < payload.size(); ++i) payload[i] = (i * 2654435761u) ^ 0xA5A5A5A5u;

	const auto byteSize = payload.size() * sizeof(uint32_t);

	const auto buffer = fixture.manager->CreateStructBuffer(
		StructBufferDesc{}.SetElement<uint32_t>().SetElementCount(
			static_cast<uint32_t>(payload.size())));
	REQUIRE_FALSE(buffer.IsNull());

	const auto readback = fixture.manager->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = byteSize, .debugName = "roundtrip" });
	REQUIRE_FALSE(readback.IsNull());

	fixture.list->Open(fixture.queue.Get(), fixture.allocator.Get());
	fixture.list->WriteBuffer(buffer, payload.data(), 0, byteSize);
	fixture.list->CopyBufferToReadback(readback, buffer);
	fixture.list->Close();

	const auto fence = fixture.queue->ExecuteCommandList(fixture.list.Get());
	fixture.queue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(fixture.manager->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	REQUIRE(std::equal(payload.begin(), payload.end(), mapped));

	fixture.manager->UnmapReadback(readback);
}

TEST_CASE("A submission moves the fence forward", "[wgpu][fence]")
{
	auto fixture = Fixture{};

	const auto before = fixture.queue->GetLastCompletedFence();

	fixture.list->Open(fixture.queue.Get(), fixture.allocator.Get());
	fixture.list->Close();

	const auto fence = fixture.queue->ExecuteCommandList(fixture.list.Get());

	REQUIRE(fence > before);
	REQUIRE(fixture.queue->GetNextFenceValue() > fence);

	fixture.queue->WaitForFenceCPUBlocking(fence);

	REQUIRE(fixture.queue->IsFenceComplete(fence));
	REQUIRE(fixture.queue->GetLastCompletedFence() >= fence);
}

TEST_CASE("A destroyed buffer's handle goes stale immediately", "[wgpu][buffer]")
{
	auto fixture = Fixture{};

	const auto buffer = fixture.manager->CreateStructBuffer(
		StructBufferDesc{}.SetElement<uint32_t>().SetElementCount(4));

	REQUIRE(fixture.manager->ValidBufferHandle(buffer));

	// Deferred: the slot is not reclaimed until the queue passes, but the handle stales now, so
	// a stale copy cannot address whatever takes the slot next.
	fixture.manager->DestroyBuffer(buffer, true);

	REQUIRE_FALSE(fixture.manager->ValidBufferHandle(buffer));

	fixture.queue->Flush();
	fixture.manager->CleanupExpiredResources();

	REQUIRE_FALSE(fixture.manager->ValidBufferHandle(buffer));
}
