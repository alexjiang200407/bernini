#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "resource/ResourceManager.h"

#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// Render targets through the RHI: make a render-attachment texture and an RTV over it, clear the
// RTV to a known colour with a render pass, copy it into a readback buffer, and confirm every texel
// is that colour. Exercises CreateRtv + the render-pass clear path the raster work builds on.
TEST_CASE("An RTV clears to a colour that reads back", "[wgpu][rtv]")
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
	texDesc.usage     = TextureUsage(TextureUsageFlag::kRenderTarget);
	texDesc.debugName = "rt";

	const auto texture = resources->CreateTexture(texDesc);
	REQUIRE(resources->ValidTextureHandle(texture));

	const auto rtv = resources->CreateRtv(texture, RtvDesc{ .format = Format::RGBA8_UNORM });
	REQUIRE(resources->ValidRtvHandle(rtv));
	REQUIRE(resources->GetRtvTexture(rtv).slot == texture.slot);

	// Opaque orange: (1.0, 0.5, 0.0, 1.0) -> RGBA8 0xFF0080FF little-endian.
	float              clear[4] = { 1.0f, 0.5f, 0.0f, 1.0f };
	constexpr uint32_t c_Expect = 0xFF0080FFu;

	const auto layout   = resources->GetTextureReadbackLayout(texture);
	const auto readback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = layout.totalBytes, .debugName = "rt-readback" });

	list->Open(queue.Get(), allocator.Get());
	resources->ClearRtv(list.Get(), rtv, clear);
	list->CopyTextureToReadback(readback, texture);
	list->Close();

	const auto fence = queue->ExecuteCommandList(list.Get());
	queue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(resources->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	bool allMatch = true;
	for (uint32_t i = 0; i < c_Size * c_Size; ++i) allMatch = allMatch && (mapped[i] == c_Expect);
	CHECK(allMatch);

	resources->UnmapReadback(readback);

	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}

// A depth target: make a Depth32Float texture and a DSV over it, and clear it through a depth
// render pass. Depth can't be copied to a buffer as simply as colour (aspect rules), so this checks
// the DSV/clear path is structurally valid -- Dawn raises a validation error on a bad depth
// attachment -- rather than reading the depth values back.
TEST_CASE("A DSV clears depth without a validation error", "[wgpu][dsv]")
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
	texDesc.format    = Format::D32;
	texDesc.usage     = TextureUsage(TextureUsageFlag::kDepthStencil);
	texDesc.debugName = "depth";

	const auto texture = resources->CreateTexture(texDesc);
	REQUIRE(resources->ValidTextureHandle(texture));

	const auto dsv = resources->CreateDsv(texture, DsvDesc{ .format = Format::D32 });
	REQUIRE(resources->ValidDsvHandle(dsv));
	REQUIRE(resources->GetDsvTexture(dsv).slot == texture.slot);

	const wgpu::Device& handle = device->GetHandle();
	handle.PushErrorScope(wgpu::ErrorFilter::Validation);

	list->Open(queue.Get(), allocator.Get());
	resources->ClearDsv(list.Get(), dsv, 0.5f, 0);
	list->Close();
	const auto fence = queue->ExecuteCommandList(list.Get());
	queue->WaitForFenceCPUBlocking(fence);

	auto       error  = std::string();
	const auto future = handle.PopErrorScope(
		wgpu::CallbackMode::WaitAnyOnly,
		[&error](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView message) {
			if (type != wgpu::ErrorType::NoError)
				error = std::string(std::string_view(message));
		});
	device->GetInstance().WaitAny(future, UINT64_MAX);

	INFO("Dawn: " << error);
	REQUIRE(error.empty());

	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}
