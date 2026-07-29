#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device_wgpu.h"
#include "pipeline/ComputeKernel.h"
#include "resource/ResourceManager.h"
#include "types/ComputeState.h"

#include <bgl/PsoType.h>
#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The end-to-end gate for the WebGPU binding model: a real compute dispatch through the RHI, on the
// GPU, checked on the bytes it produces -- not just that the pipeline built. PrefixSumInstances does
// an inclusive scan over c_PsoCount uints, so an all-ones input must read back as [1,2,3,...].
//
// This exercises the whole path the plan's step 4 adds: runtime Slang->WGSL compile, reflection into
// a bind group layout, Uniforms handle-writes resolved to bind-group entries, pipeline, dispatch,
// and readback.
TEST_CASE("A compute kernel dispatches through the RHI and writes correct data", "[wgpu][compute]")
{
	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	constexpr uint32_t c_Count  = c_PsoCount;
	const auto         byteSize = c_Count * sizeof(uint32_t);

	const auto buffer = resources->CreateComputeBuffer(
		ComputeBufferDesc{}.SetElement<uint32_t>().SetInitialCount(c_Count).SetDebugName("prefix"));
	REQUIRE(resources->ValidBufferHandle(buffer));

	auto kernel = device->CreateComputeKernel(
		ComputePipelineDesc{}
			.SetShader(device->CreateShader(ShaderDesc{}.SetSlangModuleName("PrefixSumInstances")))
			.SetDebugName("PrefixSumInstances"),
		resources);

	kernel["gUniforms"]["inOutBuffer"] = buffer;

	auto state   = ComputeState{};
	state.kernel = &kernel;

	const auto input = std::vector<uint32_t>(c_Count, 1u);

	const auto readback = resources->CreateReadbackBuffer(
		ReadbackBufferDesc{ .byteSize = byteSize, .debugName = "prefix-readback" });

	list->Open(queue.Get(), allocator.Get());
	list->WriteBuffer(buffer, input.data(), 0, byteSize);
	list->SetComputeState(state);
	list->Dispatch(1, 1, 1);
	list->CopyBufferToReadback(readback, buffer);
	list->Close();

	const auto fence = queue->ExecuteCommandList(list.Get());
	queue->WaitForFenceCPUBlocking(fence);

	const auto* mapped = static_cast<const uint32_t*>(resources->MapReadback(readback));
	REQUIRE(mapped != nullptr);

	for (uint32_t i = 0; i < c_Count; ++i) CHECK(mapped[i] == i + 1u);

	resources->UnmapReadback(readback);

	queue->Flush();
	resources->UnregisterQueue(queue.Get());
}

// PrefixSum has one directly-bound buffer; the other kernels exercise the paths it does not --
// wrapper primitives (PackedBuffer/Entry), a second `gDebug` cbuffer, and multi-binding groups.
// Dawn validates the reflected bind-group layout against the module's actual bindings at pipeline
// creation, so a mismatch here surfaces as a validation error rather than silently.
TEST_CASE("Every compute kernel builds a valid pipeline", "[wgpu][compute]")
{
	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});

	const wgpu::Device&   handle   = device->GetHandle();
	const wgpu::Instance& instance = device->GetInstance();

	for (const auto* module : { "PrefixSumInstances",
	                            "HistogramInstances",
	                            "CompactInstances",
	                            "CullInstances",
	                            "TransparentSort" })
	{
		handle.PushErrorScope(wgpu::ErrorFilter::Validation);

		auto kernel = device->CreateComputeKernel(
			ComputePipelineDesc{}
				.SetShader(device->CreateShader(ShaderDesc{}.SetSlangModuleName(module)))
				.SetDebugName(module),
			resources);
		REQUIRE(kernel.pipeline.IsInitialized());

		auto       error  = std::string();
		const auto future = handle.PopErrorScope(
			wgpu::CallbackMode::WaitAnyOnly,
			[&error](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView message) {
				if (type != wgpu::ErrorType::NoError)
					error = std::string(std::string_view(message));
			});
		instance.WaitAny(future, UINT64_MAX);

		INFO(module << ": " << error);
		REQUIRE(error.empty());
	}
}

// CullInstances puts wrapper buffers (PackedBuffer/Entry/Range) *after* a direct one, so their
// handles sit at non-zero cbuffer offsets -- the case where global-vs-struct-relative offset
// bookkeeping diverges. Binding all of them and dispatching exercises that: a wrong wrapper size
// makes the smart-buffer assignment throw, and a wrong offset makes Dispatch bind the wrong slot,
// which Dawn rejects as a missing/mismatched binding.
TEST_CASE("A kernel with non-first wrapper buffers binds and dispatches", "[wgpu][compute]")
{
	auto device    = core::SharedRef<Device>::Make(WgpuDeviceDesc{});
	auto resources = device->CreateResourceManager(ResourceManagerDesc{});
	auto queue     = device->CreateCommandQueue(QueueType::kGraphics);
	auto allocator = device->CreateCommandAllocator(QueueType::kGraphics);
	auto list      = device->CreateCommandList(CommandListDesc{}, allocator, resources);

	resources->RegisterQueue(queue.Get());

	auto kernel = device->CreateComputeKernel(
		ComputePipelineDesc{}
			.SetShader(device->CreateShader(ShaderDesc{}.SetSlangModuleName("CullInstances")))
			.SetDebugName("CullInstances"),
		resources);

	// A distinct backing buffer per binding; the shader reads garbage, but WebGPU bounds-checks
	// storage access, so a bound-but-unmeaningful buffer is safe. This test is about binding, not
	// cull semantics.
	auto       buffers = std::vector<BufferHandle>();
	const auto bind    = [&](Uniforms& uniforms, const char* field) {
		const auto handle = resources->CreateComputeBuffer(
			ComputeBufferDesc{}.SetElement<uint32_t>().SetInitialCount(256).SetDebugName(field));
		buffers.push_back(handle);
		uniforms[field] = handle;
	};

	bind(kernel["gUniforms"], "cullView");
	bind(kernel["gUniforms"], "instanceBuffer");  // PackedBuffer, non-first
	bind(kernel["gUniforms"], "meshBuffer");      // EntryBuffer, non-first
	bind(kernel["gUniforms"], "submeshBuffer");   // RangeBuffer, non-first
	bind(kernel["gUniforms"], "visibility");
	bind(kernel["gUniforms"], "stats");
	bind(kernel["gDebug"], "buffer");

	auto state   = ComputeState{};
	state.kernel = &kernel;

	const wgpu::Device& handle = device->GetHandle();
	handle.PushErrorScope(wgpu::ErrorFilter::Validation);

	list->Open(queue.Get(), allocator.Get());
	list->SetComputeState(state);
	list->Dispatch(1, 1, 1);
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
