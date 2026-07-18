#include "device/Device_metal.h"

#include "cmd/CommandAllocator_metal.h"
#include "cmd/CommandQueue_metal.h"

#include "cmd/CommandList.h"
#include "pipeline/ComputePipeline.h"
#include "pipeline/MeshletPipeline.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"

namespace bgl
{
	Device::Device(MTL::Device* device) : m_Device(NS::RetainPtr(device)) {}

	core::SharedRef<ICommandQueue>
	Device::CreateCommandQueue(QueueType) const noexcept
	{
		return core::SharedRef<CommandQueue>::Make(m_Device.get());
	}

	core::SharedRef<ICommandAllocator>
	Device::CreateCommandAllocator(QueueType) const noexcept
	{
		return core::SharedRef<CommandAllocator>::Make();
	}

	core::SharedRef<ICommandList>
	Device::CreateCommandList(
		const CommandListDesc&,
		core::SharedRef<ICommandAllocator>,
		core::SharedRef<IResourceManager>) const noexcept
	{
		gfatal("Metal backend: CreateCommandList not implemented yet");
		return nullptr;
	}

	core::SharedRef<IResourceManager>
	Device::CreateResourceManager(const ResourceManagerDesc&, core::SharedRef<ICommandQueue>)
		const noexcept
	{
		gfatal("Metal backend: CreateResourceManager not implemented yet");
		return nullptr;
	}

	core::SharedRef<IShader>
	Device::CreateShader(ShaderDesc) const noexcept
	{
		gfatal("Metal backend: CreateShader not implemented yet");
		return nullptr;
	}

	core::SharedRef<IComputePipeline>
	Device::CreateComputePipeline(const ComputePipelineDesc&) const noexcept
	{
		gfatal("Metal backend: CreateComputePipeline not implemented yet");
		return nullptr;
	}

	core::SharedRef<IMeshletPipeline>
	Device::CreateMeshletPipeline(const MeshletPipelineDesc&) const noexcept
	{
		gfatal("Metal backend: CreateMeshletPipeline not implemented yet");
		return nullptr;
	}

	Uniforms
	Device::CreateUniforms(IMeshletPipeline const*, const std::string&) const noexcept
	{
		gfatal("Metal backend: CreateUniforms not implemented yet");
		return Uniforms{};
	}

	Uniforms
	Device::CreateUniforms(IComputePipeline const*, const std::string&) const noexcept
	{
		gfatal("Metal backend: CreateUniforms not implemented yet");
		return Uniforms{};
	}
}
