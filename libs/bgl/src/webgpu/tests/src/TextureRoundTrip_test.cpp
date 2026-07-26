#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "resource/ResourceManager.h"

#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// Textures through the RHI: create a 2D texture, upload known pixels, copy it into a readback
// buffer, and read them back unchanged. 64 x RGBA8 is exactly 256 bytes per row, so the copy needs
// no row padding; the readback layout still computes the 256-aligned pitch either way.
TEST_CASE("Bytes written to a texture read back unchanged", "[wgpu][texture]")
{
	constexpr uint32_t c_Size = 64;

	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	auto texDesc      = TextureDesc{};
	texDesc.width     = c_Size;
	texDesc.height    = c_Size;
	texDesc.format    = Format::RGBA8_UNORM;
	texDesc.usage     = TextureUsageFlag::kSRV;
	texDesc.debugName = "roundtrip";

	const auto texture = resources->CreateTexture(texDesc);
	REQUIRE(resources->ValidTextureHandle(texture));

	auto pixels = std::vector<uint32_t>(c_Size * c_Size);
	for (uint32_t i = 0; i < pixels.size(); ++i) pixels[i] = (i * 2654435761u) ^ 0x5A5A5A5Au;

	const auto layout = resources->GetTextureReadbackLayout(texture);
	REQUIRE(layout.rowPitch == c_Size * 4);  // 256, already aligned

	const auto readback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = layout.totalBytes, .debugName = "tex-readback" });

	auto sub       = TextureSubresourceData{};
	sub.data       = pixels.data();
	sub.rowPitch   = c_Size * 4;
	sub.slicePitch = sub.rowPitch * c_Size;

	list->Open(queue.Get(), allocator.Get());
	list->WriteTexture(texture, std::span<const TextureSubresourceData>(&sub, 1));
	list->CopyTextureToReadback(readback, texture);
	list->Close();

	const auto fence = queue->ExecuteCommandList(list.Get());
	queue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(resources->MapReadback(readback));
	REQUIRE(mapped != nullptr);
	REQUIRE(std::equal(pixels.begin(), pixels.end(), mapped));

	resources->UnmapReadback(readback);

	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}
