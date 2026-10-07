// The Vulkan queue, allocator and command list on their own, before a device hands them out: a
// recording executed and read back, a kernel dispatched through the bindless table, a wait between
// two queues, how owners are spread over the context's queues, and a timed span.
//
// bgpu_tests globs every .cpp under tests/ whatever the backend, so a Vulkan-only case has to exclude
// itself: the headers below do not exist on any other build.
#if defined(RENDERER_BACKEND_VULKAN)

#	include "cmd/CommandAllocator_vulkan.h"
#	include "cmd/CommandList_vulkan.h"
#	include "cmd/CommandQueue_vulkan.h"
#	include "cmd/TimestampHeap_vulkan.h"
#	include "convert_vulkan.h"
#	include "native_device_vulkan.h"
#	include "pipeline/ComputePipeline_vulkan.h"
#	include "resource/ResourceManager_vulkan.h"
#	include "volk_vulkan.h"

#	include <array>
#	include <bgpu/GpuContext.h>
#	include <bgpu/cmd/CommandList.h>
#	include <bgpu/pipeline/ComputeKernel.h>
#	include <bgpu/pipeline/ComputePipeline.h>
#	include <bgpu/resource/Buffer.h>
#	include <bgpu/resource/NativeBufferDesc.h>
#	include <bgpu/resource/NativeTextureDesc.h>
#	include <bgpu/resource/Readback.h>
#	include <bgpu/resource/ResourceManager.h>
#	include <bgpu/resource/Shader.h>
#	include <bgpu/resource/Texture.h>
#	include <bgpu/types/Barrier.h>
#	include <bgpu/types/ComputeState.h>
#	include <bgpu/types/Format.h>
#	include <bgpu/types/NativeObject.h>
#	include <bgpu/types/QueueType.h>
#	include <bgpu/uniforms/Uniforms.h>
#	include <catch2/catch_test_macros.hpp>
#	include <core/ref/SharedRef.h>
#	include <cstdint>
#	include <cstring>
#	include <set>
#	include <string>
#	include <utility>
#	include <vector>

namespace
{
	bgpu::GpuContextRef
	DebugContext()
	{
		auto desc             = bgpu::GpuContextDesc();
		desc.enableDebugLayer = true;
		desc.strictError      = true;
		return bgpu::CreateGpuContext(desc);
	}

	// One owner's submission objects, made as a device would make them.
	struct Owner
	{
		explicit Owner(
			const bgpu::GpuContextRef&       context,
			bgpu::QueueType                  type,
			const bgpu::ResourceManagerDesc& desc = bgpu::ResourceManagerDesc::ComputeOnly()) :
			rm(core::SharedRef<bgpu::ResourceManager>::Make(context, desc)),
			queue(core::SharedRef<bgpu::CommandQueue>::Make(context, type)),
			alloc(core::SharedRef<bgpu::CommandAllocator>::Make(context)),
			list(core::SharedRef<bgpu::CommandList>::Make(bgpu::CommandListDesc{ type }, rm))
		{
			rm->RegisterQueue(queue.Get());
		}

		~Owner()
		{
			queue->Flush();
			rm->UnregisterQueue(queue.Get());
		}

		Owner(const Owner&) = delete;
		Owner(Owner&&)      = delete;
		Owner&
		operator=(const Owner&) = delete;
		Owner&
		operator=(Owner&&) = delete;

		void
		Run(const auto& record)
		{
			list->Open(queue.Get(), alloc.Get());
			record(*list);
			list->Close();
			queue->WaitForFenceCPUBlocking(queue->ExecuteCommandList(list.Get()));
			alloc->ResetAllocator();
		}

		core::SharedRef<bgpu::ResourceManager>  rm;
		core::SharedRef<bgpu::CommandQueue>     queue;
		core::SharedRef<bgpu::CommandAllocator> alloc;
		core::SharedRef<bgpu::CommandList>      list;
	};

	bgpu::BufferBarrierDesc
	CopyToCopy()
	{
		return bgpu::BufferBarrierDesc()
		    .AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
		    .AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
		    .AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
		    .AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource);
	}

	bgpu::ReadbackBufferHandle
	MakeReadback(bgpu::IResourceManager& rm, const uint64_t byteSize)
	{
		auto desc      = bgpu::ReadbackBufferDesc();
		desc.byteSize  = byteSize;
		desc.debugName = "readback";
		return rm.CreateReadbackBuffer(desc);
	}

	std::vector<uint32_t>
	ReadBack(bgpu::IResourceManager& rm, const bgpu::ReadbackBufferHandle rb, const uint32_t count)
	{
		const auto* mapped = static_cast<const uint32_t*>(rm.MapReadback(rb));
		auto        values = std::vector<uint32_t>(mapped, mapped + count);
		rm.UnmapReadback(rb);
		return values;
	}
}

TEST_CASE("A Vulkan list writes, copies and reads back a buffer", "[vulkan][submit]")
{
	auto context = DebugContext();
	auto owner   = Owner(context, bgpu::QueueType::kCompute);

	constexpr auto c_Values = std::to_array<uint32_t>({ 3, 1, 4, 1, 5, 9, 2, 6 });
	const auto     words    = bgpu::StructBufferDesc().SetElement<uint32_t>().SetElementCount(8);
	const auto     staged =
		owner.rm->CreateStructBuffer(bgpu::StructBufferDesc(words).SetDebugName("staged"));
	const auto copied =
		owner.rm->CreateStructBuffer(bgpu::StructBufferDesc(words).SetDebugName("copied"));
	const auto rb = MakeReadback(*owner.rm, sizeof(c_Values));

	owner.Run([&](bgpu::ICommandList& list) {
		list.WriteBuffer(staged, c_Values.data(), sizeof(c_Values));
		list.Barrier(staged, CopyToCopy());
		list.CopyBuffer(copied, staged, 0, 0, sizeof(c_Values));
		list.Barrier(copied, CopyToCopy());
		list.CopyBufferToReadback(rb, copied);
	});

	CHECK(ReadBack(*owner.rm, rb, 8) == std::vector<uint32_t>(c_Values.begin(), c_Values.end()));

	owner.rm->DestroyReadbackBuffer(rb, false);
	owner.rm->DestroyBuffer(copied, false);
	owner.rm->DestroyBuffer(staged, false);
}

// The constant buffer is a uniform-buffer descriptor written per dispatch, and the buffer it names is
// reached through the manager's bindless table: the kernel writes 100 + i from `first` on.
TEST_CASE("A Vulkan kernel writes through the bindless table", "[vulkan][submit][compute]")
{
	auto context = DebugContext();
	auto owner   = Owner(context, bgpu::QueueType::kCompute);

	auto shader            = bgpu::ShaderDesc();
	shader.slangModuleName = "bgpu.CSWriteEntryRange";
	shader.entryPointName  = "main";
	shader.debugName       = "bgpu.CSWriteEntryRange:main";

	auto kernel     = bgpu::ComputeKernel();
	kernel.pipeline = core::SharedRef<bgpu::ComputePipeline>::Make(
		context,
		nullptr,
		bgpu::ComputePipelineDesc().SetShader(
			core::SharedRef<bgpu::Shader>::Make(shader, context)));
	for (const std::string& name : kernel.pipeline->GetUniformBufferNames())
		kernel.uniforms.try_emplace(name, bgpu::Uniforms(kernel.pipeline.Get(), name));

	constexpr uint32_t c_Count = 8;
	const auto         out     = owner.rm->CreateComputeBuffer(
		bgpu::ComputeBufferDesc().SetElement<uint32_t>().SetInitialCount(c_Count).SetDebugName(
			"out"));
	const auto rb = MakeReadback(*owner.rm, c_Count * sizeof(uint32_t));

	kernel["gUniforms"]["values"] = out;
	kernel["gUniforms"]["first"]  = 2U;
	kernel["gUniforms"]["count"]  = c_Count - 2;

	owner.Run([&](bgpu::ICommandList& list) {
		auto state   = bgpu::ComputeState();
		state.kernel = &kernel;
		list.SetComputeState(state);
		list.Dispatch(1, 1, 1);
		list.Barrier(
			out,
			bgpu::BufferBarrierDesc()
				.AddSyncBefore(bgpu::BarrierSyncFlag::kComputeShader)
				.AddAccessBefore(bgpu::BarrierAccessFlag::kUnorderedAccess)
				.AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
				.AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource));
		list.CopyBufferToReadback(rb, out);
	});

	const std::vector<uint32_t> values = ReadBack(*owner.rm, rb, c_Count);
	for (uint32_t i = 2; i < c_Count; ++i) CHECK(values[i] == 100 + i - 2);

	owner.rm->DestroyReadbackBuffer(rb, false);
	owner.rm->DestroyBuffer(out, false);
}

TEST_CASE("A Vulkan queue's GPU wait orders it after another queue's work", "[vulkan][submit]")
{
	auto context  = DebugContext();
	auto producer = Owner(context, bgpu::QueueType::kCompute);
	auto consumer = Owner(context, bgpu::QueueType::kCompute);

	constexpr auto c_Values = std::to_array<uint32_t>({ 7, 7, 7, 7 });
	const auto     shared   = producer.rm->CreateStructBuffer(
		bgpu::StructBufferDesc().SetElement<uint32_t>().SetElementCount(4).SetDebugName("shared"));
	const auto imported = consumer.rm->ImportNativeBuffer(
		bgpu::NativeBufferDesc()
			.SetObject(
				bgpu::NativeObjectType::kVkBuffer,
				producer.rm->GetNativeBuffer(shared, bgpu::NativeObjectType::kVkBuffer))
			.SetBuffer(bgpu::StructBufferDesc().SetElement<uint32_t>().SetElementCount(4)));
	const auto rb = MakeReadback(*consumer.rm, sizeof(c_Values));

	producer.list->Open(producer.queue.Get(), producer.alloc.Get());
	producer.list->WriteBuffer(shared, c_Values.data(), 0, sizeof(c_Values));
	producer.list->Close();
	const uint64_t written = producer.queue->ExecuteCommandList(producer.list.Get());

	consumer.queue->InsertWaitForQueueFence(producer.queue.Get(), written);
	consumer.Run([&](bgpu::ICommandList& list) { list.CopyBufferToReadback(rb, imported); });

	CHECK(ReadBack(*consumer.rm, rb, 4) == std::vector<uint32_t>(c_Values.begin(), c_Values.end()));

	consumer.rm->DestroyReadbackBuffer(rb, false);
	consumer.rm->DestroyBuffer(imported, false);
	producer.rm->DestroyBuffer(shared, false);
}

// A texture's first transition belongs to its image, not its maker: an owner that imports it before
// the maker has submitted anything submits the transition itself, and the maker, submitting later,
// does not repeat it and discard what was written. The context is strict, so a texture used while
// still UNDEFINED ends the case.
TEST_CASE(
	"A Vulkan texture imported before its maker submits is in its layout, and kept once written",
	"[vulkan][import]")
{
	auto context  = DebugContext();
	auto producer = Owner(context, bgpu::QueueType::kGraphics, bgpu::ResourceManagerDesc());
	auto consumer = Owner(context, bgpu::QueueType::kGraphics, bgpu::ResourceManagerDesc());

	auto desc           = bgpu::TextureDesc();
	desc.width          = 4;
	desc.height         = 1;
	desc.format         = bgpu::Format::RGBA8_UNORM;
	desc.initialLayout  = bgpu::BarrierLayout::kCopyDest;
	desc.debugName      = "made";
	const auto made     = producer.rm->CreateTexture(desc);
	const auto imported = consumer.rm->ImportNativeTexture(
		bgpu::NativeTextureDesc()
			.SetObject(
				bgpu::NativeObjectType::kVkImage,
				producer.rm->GetNativeTexture(made, bgpu::NativeObjectType::kVkImage))
			.SetTexture(desc));
	REQUIRE(consumer.rm->ValidTextureHandle(imported));

	constexpr auto c_Texels =
		std::to_array<uint8_t>({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 });
	const auto source     = bgpu::TextureSubresourceData{ c_Texels.data(), 16, 16 };
	const auto layout     = consumer.rm->GetTextureReadbackLayout(imported);
	const auto consumerRb = MakeReadback(*consumer.rm, layout.totalBytes);
	const auto producerRb = MakeReadback(*producer.rm, layout.totalBytes);
	const auto copyToCopy = bgpu::TextureBarrierDesc()
	                            .AddSyncBefore(bgpu::BarrierSyncFlag::kCopy)
	                            .AddAccessBefore(bgpu::BarrierAccessFlag::kCopyDest)
	                            .SetLayoutBefore(bgpu::BarrierLayout::kCopyDest)
	                            .AddSyncAfter(bgpu::BarrierSyncFlag::kCopy)
	                            .AddAccessAfter(bgpu::BarrierAccessFlag::kCopySource)
	                            .SetLayoutAfter(bgpu::BarrierLayout::kCopySource);

	consumer.Run([&](bgpu::ICommandList& list) {
		list.WriteTexture(imported, { &source, 1 });
		list.Barrier(imported, copyToCopy);
		list.CopyTextureToReadback(consumerRb, imported);
	});
	producer.Run([&](bgpu::ICommandList& list) { list.CopyTextureToReadback(producerRb, made); });

	for (const auto& [rm, rb] :
	     { std::pair{ consumer.rm.Get(), consumerRb }, std::pair{ producer.rm.Get(), producerRb } })
	{
		const auto* read = static_cast<const uint8_t*>(rm->MapReadback(rb));
		REQUIRE(read != nullptr);
		CHECK(std::memcmp(read + layout.offset, c_Texels.data(), c_Texels.size()) == 0);
		rm->UnmapReadback(rb);
	}

	producer.rm->DestroyReadbackBuffer(producerRb, false);
	consumer.rm->DestroyReadbackBuffer(consumerRb, false);
	consumer.rm->DestroyTexture(imported, false);
	producer.rm->DestroyTexture(made, false);
}

// Two owners on one queue submit under one lock, and one with a wait on a later submission of its own
// can stall: a queue is shared only once every queue of every family that serves the type is taken.
TEST_CASE("Vulkan owners share a queue only when every one is taken", "[vulkan][submit]")
{
	auto                        context  = DebugContext();
	const auto                  families = bgpu::GetVulkanQueueFamilies(*context);
	const std::vector<uint32_t> compute =
		bgpu::QueueFamiliesFor(bgpu::QueueType::kCompute, families);

	uint32_t available = 0;
	for (const uint32_t family : compute) available += families[family].queueCount;

	auto taken    = std::vector<bgpu::VulkanQueue>();
	auto distinct = std::set<VkQueue>();
	for (uint32_t i = 0; i < available; ++i)
	{
		taken.push_back(bgpu::AcquireVulkanQueue(*context, compute));
		distinct.insert(taken.back().queue);
	}
	CHECK(distinct.size() == available);
	// The first taken is the most preferred family's.
	CHECK(taken.front().family == compute.front());

	taken.push_back(bgpu::AcquireVulkanQueue(*context, compute));
	CHECK(distinct.contains(taken.back().queue));

	for (const bgpu::VulkanQueue& queue : taken) bgpu::ReleaseVulkanQueue(*context, queue);
}

TEST_CASE("A timed Vulkan span writes both of its slots", "[vulkan][submit][timing]")
{
	auto context = DebugContext();
	if (!bgpu::TimestampHeap::Supported(*context))
		SKIP("The device cannot time a span");

	auto       owner  = Owner(context, bgpu::QueueType::kCompute);
	auto       heap   = core::SharedRef<bgpu::TimestampHeap>::Make(context, 2);
	const auto buffer = owner.rm->CreateStructBuffer(
		bgpu::StructBufferDesc().SetElement<uint32_t>().SetElementCount(1024).SetDebugName(
			"timed"));
	const auto words = std::vector<uint32_t>(1024, 1);

	owner.Run([&](bgpu::ICommandList& list) {
		list.BeginTiming(*heap, 0, 1);
		list.WriteBuffer(buffer, words.data(), words.size() * sizeof(uint32_t));
		CHECK(list.EndTiming());
		list.ResolveTimestamps(*heap, 0, 2);
	});

	auto ticks = std::array<uint64_t, 2>{};
	heap->Read(0, ticks);
	CHECK(ticks[0] != bgpu::ITimestampHeap::c_UnwrittenTimestamp);
	CHECK(ticks[1] >= ticks[0]);
	CHECK(owner.queue->GetTimestampFrequency() > 0.0);

	owner.rm->DestroyBuffer(buffer, false);
}

#endif
