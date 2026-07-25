#include "device/Device_wgpu.h"

#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The "validate under Tint" gate: slangc emitting WGSL is not the bar -- Dawn's own front-end
// (Tint) is. ValidateWgsl feeds WGSL to shader-module creation inside a validation error scope and
// returns Dawn's message; an empty string means Dawn accepted it.
//
// These cases pin the binding-model decision for the WebGPU backend: the D3D12-style bindless
// descriptor heap slangc emits for `.Handle` buffers is *rejected* by core WGSL, while a plainly
// bound storage buffer is accepted. That is why the WGSL path must bind each buffer explicitly
// rather than through the heap -- see docs/slang_shaders.md.

namespace
{
	std::string
	ValidateWgsl(Device& device, const char* wgsl)
	{
		const wgpu::Device&   handle   = device.GetHandle();
		const wgpu::Instance& instance = device.GetInstance();

		handle.PushErrorScope(wgpu::ErrorFilter::Validation);

		auto wgslDesc = wgpu::ShaderSourceWGSL{};
		wgslDesc.code = wgsl;

		auto desc        = wgpu::ShaderModuleDescriptor{};
		desc.nextInChain = &wgslDesc;
		handle.CreateShaderModule(&desc);

		auto error = std::string();

		const auto future = handle.PopErrorScope(
			wgpu::CallbackMode::WaitAnyOnly,
			[&error](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView message) {
				if (type != wgpu::ErrorType::NoError)
					error = std::string(std::string_view(message));
			});

		instance.WaitAny(future, UINT64_MAX);

		return error;
	}

	// A plainly bound storage buffer: the shape a non-bindless WGSL binding model produces, and
	// exactly what slangc emits for a `RWStructuredBuffer<uint>` (no `.Handle`).
	constexpr auto c_PlainBinding = R"(
@group(0) @binding(0) var<storage, read_write> data : array<u32>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id : vec3<u32>) {
    data[id.x] = data[id.x] + 1u;
}
)";

	// Exactly what slangc emits for the compute kernels today: the D3D12-style bindless descriptor
	// heap -- a nested runtime-sized array at module scope. Not legal core WGSL.
	constexpr auto c_BindlessHeap = R"(
@binding(0) @group(1) var _slang_resource_heap_0 : array<array<u32>>;

struct Uniforms_std140_0 { @align(16) inOutBuffer_0 : array<u32>, };
@binding(0) @group(0) var<uniform> gUniforms_0 : Uniforms_std140_0;

@compute @workgroup_size(1)
fn main(@builtin(local_invocation_id) idx : vec3<u32>) {
    _slang_resource_heap_0[gUniforms_0.inOutBuffer_0.x][idx.x] = idx.x;
}
)";
}

TEST_CASE("Tint accepts a plainly bound storage buffer", "[wgpu][tint]")
{
	auto device = core::SharedRef<Device>::Make(WgpuDeviceDesc{});

	const auto error = ValidateWgsl(*device, c_PlainBinding);
	INFO("Dawn reported: " << error);
	REQUIRE(error.empty());
}

TEST_CASE("Tint rejects slangc's bindless descriptor heap", "[wgpu][tint]")
{
	auto device = core::SharedRef<Device>::Make(WgpuDeviceDesc{});

	// Documents the constraint driving the binding model: the `.Handle` heap cannot be used on the
	// WGSL path. If Dawn ever accepts this, revisit whether bindless is viable.
	const auto error = ValidateWgsl(*device, c_BindlessHeap);
	REQUIRE_FALSE(error.empty());
}
