#include "device/Device.h"
#include "cmd/CommandQueue.h"
#include "resource/Shader.h"

namespace bgl
{
	ShaderHandle
	IDevice::CreateShader(std::string path, std::string moduleName, std::string entryPointName)
		const noexcept
	{
		auto desc            = ShaderDesc();
		desc.bytecode        = core::file::readFileBytes(path);
		desc.entryPointName  = std::move(entryPointName);
		desc.debugName       = std::move(path);
		desc.slangModuleName = std::move(moduleName);

		return CreateShader(std::move(desc));
	}

	CommandQueueHandle
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
		for (const std::string& name : kernel.pipeline->GetUniformBufferNames())
		{
			kernel.uniforms.try_emplace(name, CreateUniforms(kernel.pipeline.Get(), name));
		}
		return kernel;
	}
}
