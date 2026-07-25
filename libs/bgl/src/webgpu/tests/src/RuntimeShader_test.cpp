#include "device/Device_wgpu.h"
#include "resource/Shader_wgpu.h"
#include "shader/WgslCompile_wgpu.h"

#include <catch2/catch_test_macros.hpp>

using namespace bgl;

// The runtime Slang->WGSL path, end to end: the device's WGSL session loads a kernel module off the
// staged sources, links its entry point, and emits WGSL, which is then created as a shader module on
// the Dawn device -- so this exercises both the runtime compile and the Tint gate (a rejected module
// surfaces on the validation error scope), unlike the build-time .wgsl check.

namespace
{
	std::string
	CreateModuleAndValidate(Device& device, std::string_view wgsl)
	{
		const wgpu::Device&   handle   = device.GetHandle();
		const wgpu::Instance& instance = device.GetInstance();

		handle.PushErrorScope(wgpu::ErrorFilter::Validation);

		auto wgslDesc    = wgpu::ShaderSourceWGSL{};
		wgslDesc.code    = wgsl;
		auto desc        = wgpu::ShaderModuleDescriptor{};
		desc.nextInChain = &wgslDesc;
		handle.CreateShaderModule(&desc);

		auto       error  = std::string();
		const auto future = handle.PopErrorScope(
			wgpu::CallbackMode::WaitAnyOnly,
			[&error](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView message) {
				if (type != wgpu::ErrorType::NoError)
					error = std::string(std::string_view(message));
			});
		instance.WaitAny(future, UINT64_MAX);
		return error;
	}
}

TEST_CASE("The runtime session compiles a kernel to WGSL that Dawn accepts", "[wgpu][shader]")
{
	auto device = core::SharedRef<Device>::Make(WgpuDeviceDesc{});

	for (const auto* module : { "PrefixSumInstances",
	                            "HistogramInstances",
	                            "CompactInstances",
	                            "CullInstances",
	                            "TransparentSort" })
	{
		const auto shader = device->CreateShader(ShaderDesc{}.SetSlangModuleName(module));
		REQUIRE(shader != nullptr);

		slang::IModule* slangModule = shader->GetSlangModule();
		REQUIRE(slangModule != nullptr);

		const auto wgsl = CompileEntryPointToWgsl(device->GetSlangSession(), slangModule, "main");
		REQUIRE_FALSE(wgsl.empty());

		const auto error = CreateModuleAndValidate(*device, wgsl);
		INFO(module << ": " << error);
		REQUIRE(error.empty());
	}
}
