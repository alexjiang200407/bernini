#include "device/Device.h"
#include "cmd/CommandQueue.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/MeshletKernel.h"
#include "resource/Shader.h"
#include "types/QueueType.h"
#include <bgl_common/SurfaceReflection.h>
#include <bgpu/GpuContext.h>
#include <core/err/util.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace bgl
{
	ShaderRef
	IDevice::CreateShader(std::string slangModuleName, std::string entryPointName) const noexcept
	{
		auto desc            = ShaderDesc();
		desc.debugName       = slangModuleName + ":" + entryPointName;
		desc.entryPointName  = std::move(entryPointName);
		desc.slangModuleName = std::move(slangModuleName);

		return CreateShader(std::move(desc));
	}

	CommandQueueRef
	IDevice::CreateGraphicsCommandQueue() const noexcept
	{
		return CreateCommandQueue(QueueType::kGraphics);
	}

	ComputeKernel
	IDevice::CreateComputeKernel(const ComputePipelineDesc& desc) const noexcept
	{
		ComputeKernel kernel;
		kernel.pipeline = CreateComputePipeline(desc);
		for (const std::string& name : kernel.pipeline->GetUniformBufferNames())
		{
			kernel.uniforms.try_emplace(name, CreateUniforms(kernel.pipeline.Get(), name));
		}
		return kernel;
	}

	MeshletKernel
	IDevice::CreateMeshletKernel(const MeshletPipelineDesc& desc) const noexcept
	{
		MeshletKernel kernel;
		kernel.pipeline = CreateMeshletPipeline(desc);
		for (const auto& name : kernel.pipeline->GetUniformBufferNames())
		{
			kernel.uniforms.try_emplace(name, CreateUniforms(kernel.pipeline.Get(), name));
		}
		return kernel;
	}

	std::optional<ReflectedSurface>
	IDevice::ReflectSurfaceModule(std::string_view moduleName, std::string_view surfaceName) const
	{
		// Loaded and reflected here rather than handed back, because a slang::IModule only lives as
		// long as the session that parsed it, and the next AddSourceModule drops that.
		std::string     diagnostic;
		slang::IModule* slangModule =
			GetGpuContext().LoadScalarLayoutModule(moduleName, diagnostic);
		if (slangModule == nullptr)
		{
			core::throw_runtime_error(
				"surface '{}': its module did not compile\n{}",
				surfaceName,
				diagnostic);
		}

		return ReflectSurface(slangModule, surfaceName);
	}
}
