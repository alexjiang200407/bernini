// Held through SharedRef via `auto` and dereferenced: both need the complete type, which
// include-cleaner cannot see through the template.
#include <array>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandAllocator.h>  // IWYU pragma: keep
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>  // IWYU pragma: keep
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/Color.h>
#include <bgpu/types/Format.h>
#include <bgpu/types/MeshletState.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/types/Viewport.h>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

// Where a mesh dispatch draws, and how many groups an indirect one runs: what a renderer's draws
// rest on, with no renderer in the process.
namespace
{
	constexpr uint32_t c_Size = 4;

	struct Target
	{
		bgpu::TextureHandle         texture;
		bgpu::RtvHandle             rtv;
		bgpu::ReadbackBufferHandle  readback;
		bgpu::TextureReadbackLayout layout;
	};

	struct Owner
	{
		bgpu::GpuContextRef       context;
		bgpu::DeviceRef           device;
		bgpu::ResourceManagerRef  rm;
		bgpu::CommandQueueRef     queue;
		bgpu::CommandAllocatorRef alloc;
		bgpu::CommandListRef      list;
		bgpu::MeshletKernel       kernel;

		Owner()
		{
			auto contextDesc             = bgpu::GpuContextDesc();
			contextDesc.enableDebugLayer = true;
			context                      = bgpu::CreateGpuContext(contextDesc);
			device                       = bgpu::CreateDevice(context);
			rm    = device->CreateResourceManager(bgpu::ResourceManagerDesc());
			queue = device->CreateCommandQueue(bgpu::QueueType::kGraphics);
			rm->RegisterQueue(queue.Get());

			auto listDesc = bgpu::CommandListDesc();
			listDesc.type = bgpu::QueueType::kGraphics;
			alloc         = device->CreateCommandAllocator(bgpu::QueueType::kGraphics);
			list          = device->CreateCommandList(listDesc, alloc, rm);

			auto pipelineDesc =
				bgpu::MeshletPipelineDesc()
					.SetMeshShader(device->CreateShader("bgpu.MeshTopHalf", "MSMain"))
					.SetPixelShader(device->CreateShader("bgpu.MeshTopHalf", "PSMain"))
					.AddRtvFormat(bgpu::Format::RGBA8_UNORM);
			// Culling off: what is pinned here is where the quad lands, not which way it winds.
			pipelineDesc.renderState.rasterState.SetCullNone();
			kernel = device->CreateMeshletKernel(pipelineDesc);
		}

		~Owner() { rm->UnregisterQueue(queue.Get()); }

		Owner(const Owner&) = delete;
		Owner(Owner&&)      = delete;
		Owner&
		operator=(const Owner&) = delete;
		Owner&
		operator=(Owner&&) = delete;

		Target
		MakeTarget(const char* name)
		{
			auto desc          = bgpu::TextureDesc();
			desc.width         = c_Size;
			desc.height        = c_Size;
			desc.format        = bgpu::Format::RGBA8_UNORM;
			desc.usage         = bgpu::TextureUsageFlag::kRenderTarget;
			desc.initialLayout = bgpu::BarrierLayout::kRenderTarget;
			desc.debugName     = name;
			desc.clearValue.SetColor(bgpu::Color(0.0f, 0.0f, 0.0f, 1.0f));

			auto target    = Target();
			target.texture = rm->CreateTexture(desc);
			auto rtvDesc   = bgpu::RtvDesc();
			rtvDesc.format = bgpu::Format::RGBA8_UNORM;
			target.rtv     = rm->CreateRtv(target.texture, rtvDesc);
			target.layout  = rm->GetTextureReadbackLayout(target.texture);

			auto rbDesc      = bgpu::ReadbackBufferDesc();
			rbDesc.byteSize  = target.layout.totalBytes;
			rbDesc.debugName = name;
			target.readback  = rm->CreateReadbackBuffer(rbDesc);

			float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			rm->ClearRtv(list.Get(), target.rtv, clear);
			return target;
		}

		bgpu::MeshletState
		StateFor(const Target& target)
		{
			auto state   = bgpu::MeshletState();
			state.kernel = &kernel;
			state.viewportState.AddViewportAndScissorRect(
				bgpu::Viewport(static_cast<float>(c_Size), static_cast<float>(c_Size)));
			state.frameBuffer.AddColorAttachment(target.rtv);
			return state;
		}

		void
		ReadBack(const Target& target)
		{
			list->Barrier(
				target.rtv,
				bgpu::TextureBarrierDesc()
					.AddSyncBefore(bgpu::BarrierSyncFlag::kRenderTarget)
					.AddAccessBefore(bgpu::BarrierAccessFlag::kRenderTarget)
					.SetLayoutBefore(bgpu::BarrierLayout::kRenderTarget)
					.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
					.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
					.SetLayoutAfter(bgpu::BarrierLayout::kCopySource));
			list->CopyTextureToReadback(target.readback, target.texture);
		}

		void
		Submit()
		{
			list->Close();
			queue->WaitForFenceCPUBlocking(queue->ExecuteCommandList(list.Get()));
		}

		// The green channel of the first texel of `row`: 255 where the quad drew, 0 where it did not.
		uint8_t
		GreenAt(const Target& target, const uint32_t row)
		{
			const auto*   texels = static_cast<const uint8_t*>(rm->MapReadback(target.readback));
			const uint8_t green  = texels[target.layout.offset + row * target.layout.rowPitch + 1];
			rm->UnmapReadback(target.readback);
			return green;
		}

		void
		Destroy(const Target& target)
		{
			rm->DestroyReadbackBuffer(target.readback, false);
			rm->DestroyRtv(target.rtv, false);
			rm->DestroyTexture(target.texture, false);
		}
	};
}

TEST_CASE("A mesh dispatch draws where D3D's clip space puts it", "[render][rhi]")
{
	auto owner = Owner();
	REQUIRE(owner.kernel.pipeline != nullptr);

	owner.list->Open(owner.queue.Get(), owner.alloc.Get());
	const Target target = owner.MakeTarget("top half");
	owner.list->SetMeshletState(owner.StateFor(target));
	owner.list->DispatchMesh(1, 1, 1);
	owner.ReadBack(target);
	owner.Submit();

	// Clip space's +y is the target's first row, on every backend.
	CHECK(owner.GreenAt(target, 0) == 255);
	CHECK(owner.GreenAt(target, c_Size - 1) == 0);
	owner.Destroy(target);
}

TEST_CASE(
	"An indirect mesh dispatch runs the groups its arguments name, and a count of zero runs none",
	"[render][rhi]")
{
	auto owner = Owner();
	REQUIRE(owner.kernel.pipeline != nullptr);

	// Two arguments as D3D12_DISPATCH_MESH_ARGUMENTS lays them out, one group and none, and the
	// counts zero and one. The verb's precondition pairs a zero count with the zero grid: Metal
	// never reads the count and dispatches the grid as it is (CommandList.h).
	constexpr auto c_Args   = std::to_array<uint32_t>({ 1, 1, 1, 0, 0, 0 });
	constexpr auto c_Counts = std::to_array<uint32_t>({ 0, 1 });
	const auto     args     = owner.rm->CreateStructBuffer(
		bgpu::StructBufferDesc().SetElement<uint32_t>().SetElementCount(6).SetDebugName("args"));
	const auto counts = owner.rm->CreateStructBuffer(
		bgpu::StructBufferDesc().SetElement<uint32_t>().SetElementCount(2).SetDebugName("counts"));

	owner.list->Open(owner.queue.Get(), owner.alloc.Get());
	owner.list->WriteBuffer(args, c_Args.data(), 0, sizeof(c_Args));
	owner.list->WriteBuffer(counts, c_Counts.data(), 0, sizeof(c_Counts));
	const auto toIndirect = bgpu::BufferBarrierDesc()
	                            .AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
	                            .AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
	                            .AddSyncAfter(bgpu::BarrierSyncFlag::kIndirectArgument)
	                            .AddAccessAfter(bgpu::BarrierAccessFlag::kIndirectArgument);
	owner.list->Barrier(args, toIndirect);
	owner.list->Barrier(counts, toIndirect);

	const Target none     = owner.MakeTarget("count zero");
	const Target one      = owner.MakeTarget("count one");
	const Target indirect = owner.MakeTarget("indirect");

	auto state          = owner.StateFor(none);
	state.indirectArgs  = args;
	state.commandCounts = counts;
	owner.list->SetMeshletState(state);
	owner.list->DispatchMeshIndirectCount(1, 0);

	state.frameBuffer = owner.StateFor(one).frameBuffer;
	owner.list->SetMeshletState(state);
	owner.list->DispatchMeshIndirectCount(0, 1);

	state.frameBuffer = owner.StateFor(indirect).frameBuffer;
	owner.list->SetMeshletState(state);
	owner.list->DispatchMeshIndirect(0);

#if !defined(RENDERER_BACKEND_METAL)
	// A backend that reads the count skips the dispatch on zero, whatever grid the argument names.
	const Target skipped = owner.MakeTarget("count zero, one group");
	state.frameBuffer    = owner.StateFor(skipped).frameBuffer;
	owner.list->SetMeshletState(state);
	owner.list->DispatchMeshIndirectCount(0, 0);
	owner.ReadBack(skipped);
#endif

	owner.ReadBack(none);
	owner.ReadBack(one);
	owner.ReadBack(indirect);
	owner.Submit();

	CHECK(owner.GreenAt(none, 0) == 0);
	CHECK(owner.GreenAt(one, 0) == 255);
	CHECK(owner.GreenAt(indirect, 0) == 255);
#if !defined(RENDERER_BACKEND_METAL)
	CHECK(owner.GreenAt(skipped, 0) == 0);
	owner.Destroy(skipped);
#endif

	owner.Destroy(indirect);
	owner.Destroy(one);
	owner.Destroy(none);
	owner.rm->DestroyBuffer(counts, false);
	owner.rm->DestroyBuffer(args, false);
}
