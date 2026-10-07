#include "device/Device_vulkan.h"
#include "cmd/CommandAllocator_vulkan.h"
#include "cmd/CommandList_vulkan.h"
#include "cmd/CommandQueue_vulkan.h"
#include "cmd/TimestampHeap_vulkan.h"
#include "pipeline/ComputePipeline_vulkan.h"
#include "pipeline/MeshletPipeline_vulkan.h"
#include "resource/ResourceManager_vulkan.h"
#include "shadercache/ShaderCache_vulkan.h"
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/cmd/TimestampHeap.h>
#include <bgpu/device/Device.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/err/util.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace bgpu
{
	Device::Device(GpuContextRef context) : m_Context(std::move(context))
	{
		core::ensure(m_Context != nullptr, "A device needs a GPU context");
		if (m_Context->GetProgramCache() != nullptr)
			m_ShaderCache = std::make_unique<ShaderCache>(m_Context);
	}

	Device::~Device() noexcept { spdlog::trace("~Device"); }

	ShaderRef
	Device::CreateShader(ShaderDesc desc) const noexcept
	{
		return core::SharedRef<Shader>::Make(std::move(desc), m_Context);
	}

	void
	Device::AddSourceModule(const SlangSourceModule& sourceModule) noexcept
	{
		m_Context->AddSourceModule(sourceModule);
	}

	void
	Device::ReleaseSlangSession() noexcept
	{
		m_Context->ReleaseSlangSessions();
	}

	ComputePipelineRef
	Device::CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept
	{
		return core::SharedRef<ComputePipeline>::Make(m_Context, m_ShaderCache.get(), desc);
	}

	MeshletPipelineRef
	Device::CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept
	{
		return core::SharedRef<MeshletPipeline>::Make(m_Context, m_ShaderCache.get(), desc);
	}

	CommandListRef
	Device::CreateCommandList(
		const CommandListDesc& desc,
		CommandAllocatorRef    commandAllocator,
		ResourceManagerRef     resourceManager) const noexcept
	{
		core::ensure(commandAllocator != nullptr, "Command allocator cannot be null");
		core::ensure(resourceManager != nullptr, "Resource manager cannot be null");
		// A Vulkan list takes a command buffer from whichever allocator each Open names.
		return core::SharedRef<CommandList>::Make(desc, std::move(resourceManager));
	}

	CommandAllocatorRef
	Device::CreateCommandAllocator(const QueueType type) const noexcept
	{
		// Its pools are per family, made as lists open on queues of them.
		(void)type;
		return core::SharedRef<CommandAllocator>::Make(m_Context);
	}

	CommandQueueRef
	Device::CreateCommandQueue(const QueueType type) const noexcept
	{
		return core::SharedRef<CommandQueue>::Make(m_Context, type);
	}

	core::SharedRef<ITimestampHeap>
	Device::CreateTimestampHeap(const uint32_t capacity) const noexcept
	{
		if (!TimestampHeap::Supported(*m_Context))
			return nullptr;
		return core::SharedRef<TimestampHeap>::Make(m_Context, capacity);
	}

	ResourceManagerRef
	Device::CreateResourceManager(const ResourceManagerDesc& desc) const noexcept
	{
		return core::SharedRef<ResourceManager>::Make(m_Context, desc);
	}

	Uniforms
	Device::CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
		const noexcept
	{
		core::ensure(pipeline != nullptr, "Pipeline pointer cannot be null");
		return Uniforms(pipeline, cbufferName);
	}

	Uniforms
	Device::CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
		const noexcept
	{
		core::ensure(pipeline != nullptr, "Pipeline pointer cannot be null");
		return Uniforms(pipeline, cbufferName);
	}

	DeviceRef
	CreateDevice(GpuContextRef context)
	{
		return core::SharedRef<Device>::Make(std::move(context));
	}
}
