#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/types/QueueType.h>
#include <core/err/util.h>
#include <string>
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
}
