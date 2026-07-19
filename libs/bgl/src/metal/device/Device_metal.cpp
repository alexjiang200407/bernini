#include "device/Device_metal.h"

#include "cmd/CommandAllocator_metal.h"
#include "cmd/CommandList_metal.h"
#include "cmd/CommandQueue_metal.h"
#include "resource/ResourceManager_metal.h"

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
		const CommandListDesc&             desc,
		core::SharedRef<ICommandAllocator> commandAllocator,
		core::SharedRef<IResourceManager>  resourceManager) const noexcept
	{
		return core::SharedRef<CommandList>::Make(
			desc,
			commandAllocator.Get(),
			std::move(resourceManager));
	}

	core::SharedRef<IResourceManager>
	Device::CreateResourceManager(const ResourceManagerDesc& desc, core::SharedRef<ICommandQueue>)
		const noexcept
	{
		return core::SharedRef<ResourceManager>::Make(m_Device.get(), desc);
	}

	core::SharedRef<IShader>
	Device::CreateShader(ShaderDesc) const noexcept
	{
		gunimplemented("Metal backend: CreateShader not implemented yet");
		return nullptr;
	}

	core::SharedRef<IComputePipeline>
	Device::CreateComputePipeline(const ComputePipelineDesc&) const noexcept
	{
		gunimplemented("Metal backend: CreateComputePipeline not implemented yet");
		return nullptr;
	}

	core::SharedRef<IMeshletPipeline>
	Device::CreateMeshletPipeline(const MeshletPipelineDesc&) const noexcept
	{
		gunimplemented("Metal backend: CreateMeshletPipeline not implemented yet");
		return nullptr;
	}

	Uniforms
	Device::CreateUniforms(IMeshletPipeline const*, const std::string&) const noexcept
	{
		gunimplemented("Metal backend: CreateUniforms not implemented yet");
		return Uniforms{};
	}

	Uniforms
	Device::CreateUniforms(IComputePipeline const*, const std::string&) const noexcept
	{
		gunimplemented("Metal backend: CreateUniforms not implemented yet");
		return Uniforms{};
	}
}
