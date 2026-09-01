#include "device/Device.h"
#include "cmd/CommandQueue.h"
#include "resource/Shader.h"

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
}
