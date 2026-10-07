// Held through SharedRef via `auto` and dereferenced: both need the complete type, which
// include-cleaner cannot see through the template.
#include <array>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>  // IWYU pragma: keep
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Color.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/QueueType.h>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>

// A texture's bytes in and out of the GPU, and a shader reading it through a view and a sampler: the
// texture half of the RHI with no render target and no renderer in the process.
namespace
{
	// A float4 as the kernel writes it.
	struct Texel
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		float a = 0.0f;

		bool
		operator==(const Texel&) const = default;
	};

	struct Owner
	{
		bgpu::GpuContextRef       context;
		bgpu::DeviceRef           device;
		bgpu::ResourceManagerRef  rm;
		bgpu::CommandQueueRef     queue;
		bgpu::CommandAllocatorRef alloc;
		bgpu::CommandListRef      list;

		explicit Owner(const bgpu::QueueType type = bgpu::QueueType::kCompute)
		{
			auto contextDesc             = bgpu::GpuContextDesc();
			contextDesc.enableDebugLayer = true;
			context                      = bgpu::CreateGpuContext(contextDesc);
			device                       = bgpu::CreateDevice(context);
			rm    = device->CreateResourceManager(bgpu::ResourceManagerDesc());
			queue = device->CreateCommandQueue(type);
			rm->RegisterQueue(queue.Get());

			auto listDesc = bgpu::CommandListDesc();
			listDesc.type = type;
			alloc         = device->CreateCommandAllocator(type);
			list          = device->CreateCommandList(listDesc, alloc, rm);
		}

		~Owner() { rm->UnregisterQueue(queue.Get()); }

		Owner(const Owner&) = delete;
		Owner(Owner&&)      = delete;
		Owner&
		operator=(const Owner&) = delete;
		Owner&
		operator=(Owner&&) = delete;

		void
		Submit()
		{
			list->Close();
			queue->WaitForFenceCPUBlocking(queue->ExecuteCommandList(list.Get()));
		}
	};

	bgpu::TextureBarrierDesc
	CopyDestTo(
		const bgpu::BarrierSyncFlag   sync,
		const bgpu::BarrierAccessFlag access,
		const bgpu::BarrierLayout     layout)
	{
		return bgpu::TextureBarrierDesc()
		    .AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
		    .AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
		    .SetLayoutBefore(bgpu::BarrierLayout::kCopyDest)
		    .AddSyncAfter(sync)
		    .AddAccessAfter(access)
		    .SetLayoutAfter(layout);
	}
}

TEST_CASE("A texture written through a list reads back as written", "[render][rhi]")
{
	auto owner = Owner();

	// Three texels a row: twelve bytes, far below the 256 a readback row is padded to.
	constexpr uint32_t c_Width  = 3;
	constexpr uint32_t c_Height = 2;
	constexpr uint32_t c_Pitch  = c_Width * 4;

	auto desc          = bgpu::TextureDesc();
	desc.width         = c_Width;
	desc.height        = c_Height;
	desc.format        = bgpu::Format::RGBA8_UNORM;
	desc.initialLayout = bgpu::BarrierLayout::kCopyDest;
	desc.debugName     = "round trip";
	const auto texture = owner.rm->CreateTexture(desc);
	REQUIRE(owner.rm->ValidTextureHandle(texture));

	auto texels = std::array<uint8_t, c_Pitch * c_Height>();
	for (size_t i = 0; i < texels.size(); ++i) texels[i] = static_cast<uint8_t>(i * 7 + 1);

	const auto layout = owner.rm->GetTextureReadbackLayout(texture);
	CHECK(layout.rowSizeBytes == c_Pitch);
	CHECK(layout.rowPitch % 256 == 0);
	CHECK(layout.rowCount == c_Height);

	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = layout.totalBytes;
	rbDesc.debugName = "round trip readback";
	const auto rb    = owner.rm->CreateReadbackBuffer(rbDesc);

	const auto source = bgpu::TextureSubresourceData{ texels.data(), c_Pitch, c_Pitch * c_Height };

	owner.list->Open(owner.queue.Get(), owner.alloc.Get());
	owner.list->WriteTexture(texture, { &source, 1 });
	owner.list->Barrier(
		texture,
		CopyDestTo(
			bgpu::BarrierSyncFlag::kCopy,
			bgpu::BarrierAccessFlag::kCopySource,
			bgpu::BarrierLayout::kCopySource));
	owner.list->CopyTextureToReadback(rb, texture);
	owner.Submit();

	const auto* read = static_cast<const uint8_t*>(owner.rm->MapReadback(rb));
	REQUIRE(read != nullptr);
	for (uint32_t row = 0; row < c_Height; ++row)
	{
		CHECK(
			std::memcmp(
				read + layout.offset + row * layout.rowPitch,
				&texels[row * c_Pitch],
				c_Pitch) == 0);
	}
	owner.rm->UnmapReadback(rb);

	owner.rm->DestroyReadbackBuffer(rb, false);
	owner.rm->DestroyTexture(texture, false);
}

TEST_CASE("A compute kernel samples a texture through its view and a sampler", "[render][rhi]")
{
	auto owner = Owner();

	auto desc          = bgpu::TextureDesc();
	desc.width         = 2;
	desc.height        = 2;
	desc.format        = bgpu::Format::RGBA8_UNORM;
	desc.initialLayout = bgpu::BarrierLayout::kCopyDest;
	desc.debugName     = "sampled";
	const auto texture = owner.rm->CreateTexture(desc);

	auto srvDesc       = bgpu::SrvDesc();
	srvDesc.format     = bgpu::Format::RGBA8_UNORM;
	srvDesc.debugName  = "sampled view";
	const auto srv     = owner.rm->CreateSrv(texture, srvDesc);
	const auto sampler = owner.rm->CreateSampler(bgpu::SamplerDesc().SetAllFilters(false));
	REQUIRE(owner.rm->ValidSrvHandle(srv));
	REQUIRE(owner.rm->ValidSamplerHandle(sampler));

	constexpr auto c_Texels = std::to_array<uint8_t>({
		255,
		0,
		0,
		255,  //
		0,
		255,
		0,
		255,  //
		0,
		0,
		255,
		255,  //
		255,
		255,
		255,
		0,
	});
	const auto     source   = bgpu::TextureSubresourceData{ c_Texels.data(), 8, 16 };

	const auto out = owner.rm->CreateComputeBuffer(
		bgpu::ComputeBufferDesc().SetElement<Texel>().SetInitialCount(4).SetDebugName(
			"sampled out"));
	auto rbDesc      = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize  = 4 * sizeof(Texel);
	rbDesc.debugName = "sampled readback";
	const auto rb    = owner.rm->CreateReadbackBuffer(rbDesc);

	auto kernel = owner.device->CreateComputeKernel(
		bgpu::ComputePipelineDesc()
			.SetShader(owner.device->CreateShader("bgpu.CSSampleTexture"))
			.SetDebugName("bgpu.CSSampleTexture"));
	REQUIRE(kernel.pipeline != nullptr);
	kernel["gUniforms"]["texture"]      = srv;
	kernel["gUniforms"]["pointSampler"] = sampler;
	kernel["gUniforms"]["outBuffer"]    = out;

	owner.list->Open(owner.queue.Get(), owner.alloc.Get());
	owner.list->WriteTexture(texture, { &source, 1 });
	owner.list->Barrier(
		texture,
		CopyDestTo(
			bgpu::BarrierSyncFlag::kComputeShader,
			bgpu::BarrierAccessFlag::kShaderResource,
			bgpu::BarrierLayout::kShaderResource));

	auto state   = bgpu::ComputeState();
	state.kernel = &kernel;
	owner.list->SetComputeState(state);
	owner.list->Dispatch(1, 1, 1);

	owner.list->Barrier(
		out,
		bgpu::BufferBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));
	owner.list->CopyBufferToReadback(rb, out);
	owner.Submit();

	const auto* read = static_cast<const Texel*>(owner.rm->MapReadback(rb));
	REQUIRE(read != nullptr);
	for (size_t i = 0; i < 4; ++i)
	{
		const auto expected = Texel(
			c_Texels[i * 4 + 0] / 255.0f,
			c_Texels[i * 4 + 1] / 255.0f,
			c_Texels[i * 4 + 2] / 255.0f,
			c_Texels[i * 4 + 3] / 255.0f);
		CHECK(read[i] == expected);
	}
	owner.rm->UnmapReadback(rb);

	owner.rm->DestroyReadbackBuffer(rb, false);
	owner.rm->DestroyBuffer(out, false);
	owner.rm->DestroySampler(sampler, false);
	owner.rm->DestroySrv(srv, false);
	owner.rm->DestroyTexture(texture, false);
}

TEST_CASE("A cleared colour and depth target read back as their clear values", "[render][rhi]")
{
	auto owner = Owner(bgpu::QueueType::kGraphics);

	auto colorDesc          = bgpu::TextureDesc();
	colorDesc.width         = 8;
	colorDesc.height        = 8;
	colorDesc.format        = bgpu::Format::RGBA8_UNORM;
	colorDesc.usage         = bgpu::TextureUsageFlag::kRenderTarget;
	colorDesc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
	colorDesc.debugName     = "cleared color";
	colorDesc.clearValue.SetColor(bgpu::Color(0.0f, 1.0f, 0.2f, 0.6f));
	const auto color = owner.rm->CreateTexture(colorDesc);

	auto depthDesc          = bgpu::TextureDesc();
	depthDesc.width         = 8;
	depthDesc.height        = 8;
	depthDesc.format        = bgpu::Format::D32;
	depthDesc.usage         = bgpu::TextureUsageFlag::kDepthStencil;
	depthDesc.initialLayout = bgpu::BarrierLayout::kDepthWrite;
	depthDesc.debugName     = "cleared depth";
	depthDesc.clearValue.SetDepthStencil(0.25f, 0);
	const auto depth = owner.rm->CreateTexture(depthDesc);

	auto rtvDesc       = bgpu::RtvDesc();
	rtvDesc.format     = bgpu::Format::RGBA8_UNORM;
	const auto rtv     = owner.rm->CreateRtv(color, rtvDesc);
	auto       dsvDesc = bgpu::DsvDesc();
	dsvDesc.format     = bgpu::Format::D32;
	const auto dsv     = owner.rm->CreateDsv(depth, dsvDesc);
	REQUIRE(owner.rm->ValidRtvHandle(rtv));
	REQUIRE(owner.rm->ValidDsvHandle(dsv));
	CHECK(owner.rm->GetRtvTexture(rtv) == color);
	CHECK(owner.rm->GetDsvTexture(dsv) == depth);

	const auto colorLayout = owner.rm->GetTextureReadbackLayout(color);
	const auto depthLayout = owner.rm->GetTextureReadbackLayout(depth);
	CHECK(depthLayout.rowSizeBytes == 8 * sizeof(float));

	auto rbDesc        = bgpu::ReadbackBufferDesc();
	rbDesc.byteSize    = colorLayout.totalBytes;
	rbDesc.debugName   = "cleared color readback";
	const auto colorRb = owner.rm->CreateReadbackBuffer(rbDesc);
	rbDesc.byteSize    = depthLayout.totalBytes;
	rbDesc.debugName   = "cleared depth readback";
	const auto depthRb = owner.rm->CreateReadbackBuffer(rbDesc);

	owner.list->Open(owner.queue.Get(), owner.alloc.Get());
	float clear[4] = { 0.0f, 1.0f, 0.2f, 0.6f };
	owner.rm->ClearRtv(owner.list.Get(), rtv, clear);
	owner.rm->ClearDsv(owner.list.Get(), dsv, 0.25f, 0);
	owner.list->Barrier(
		rtv,
		bgpu::TextureBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kRenderTarget)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kRenderTarget)
			.SetLayoutBefore(bgpu::BarrierLayout::kRenderTarget)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
			.SetLayoutAfter(bgpu::BarrierLayout::kCopySource));
	owner.list->Barrier(
		dsv,
		bgpu::TextureBarrierDesc()
			.AddSyncBefore(bgpu::BarrierSyncFlag::kDepthStencil)
			.AddAccessBefore(bgpu::BarrierAccessFlag::kDepthWrite)
			.SetLayoutBefore(bgpu::BarrierLayout::kDepthWrite)
			.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
			.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
			.SetLayoutAfter(bgpu::BarrierLayout::kCopySource));
	owner.list->CopyTextureToReadback(colorRb, color);
	owner.list->CopyTextureToReadback(depthRb, depth);
	owner.Submit();

	const auto* texels = static_cast<const uint8_t*>(owner.rm->MapReadback(colorRb));
	REQUIRE(texels != nullptr);
	CHECK(texels[colorLayout.offset + 0] == 0);
	CHECK(texels[colorLayout.offset + 1] == 255);
	CHECK(texels[colorLayout.offset + 2] == 51);
	CHECK(texels[colorLayout.offset + 3] == 153);
	owner.rm->UnmapReadback(colorRb);

	const auto* depths = static_cast<const std::byte*>(owner.rm->MapReadback(depthRb));
	REQUIRE(depths != nullptr);
	auto first = 0.0f;
	std::memcpy(&first, depths + depthLayout.offset, sizeof(first));
	CHECK(first == 0.25f);
	owner.rm->UnmapReadback(depthRb);

	owner.rm->DestroyReadbackBuffer(depthRb, false);
	owner.rm->DestroyReadbackBuffer(colorRb, false);
	owner.rm->DestroyDsv(dsv, false);
	owner.rm->DestroyRtv(rtv, false);
	owner.rm->DestroyTexture(depth, false);
	owner.rm->DestroyTexture(color, false);
}
