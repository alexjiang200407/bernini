#include "device/Device_d3d12.h"
#include "RenderTarget_d3d12.h"
#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandList_d3d12.h"
#include "cmd/CommandQueue_d3d12.h"
#include "cmd/TimestampHeap_d3d12.h"
#include "pipeline/ComputePipeline_d3d12.h"
#include "pipeline/MeshletPipeline_d3d12.h"
#include "resource/ResourceManager_d3d12.h"
#include "shadercache/ShaderCache_d3d12.h"
#include <bgpu/GpuContext.h>
#include <bgpu/SlangErrorChecker.h>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/cmd/TimestampHeap.h>
#include <bgpu/d3d12/native_device.h>
#include <bgpu/pipeline/ComputePipeline.h>
#include <bgpu/pipeline/MeshletPipeline.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Shader.h>
#include <bgpu/types/QueueType.h>
#include <core/err/util.h>
#include <core/ref/SharedRef.h>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <string>

namespace bgl
{
	Device::Device(const bgpu::GpuContextRef& context) :
		m_Context(context), m_Device(bgpu::GetD3d12Device(*context))
	{
		core::ensure(m_Device != nullptr, "D3D12 device cannot be null");

		if (m_Context->GetProgramCache() != nullptr)
		{
			m_ShaderCache = std::make_unique<ShaderCache>(
				m_Context,
				m_Device.Get(),
				!m_Context->GpuValidationActive());
		}
	}

	void
	Device::AddSourceModule(const bgpu::SlangSourceModule& sourceModule) noexcept
	{
		m_Context->AddSourceModule(sourceModule);
	}

	void
	Device::ReleaseSlangSession() noexcept
	{
		m_Context->ReleaseSlangSessions();
	}

	Device::~Device() noexcept { spdlog::trace("~Device"); }

	CommandListRef
	Device::CreateCommandList(
		const CommandListDesc& desc,
		CommandAllocatorRef    commandAllocator,
		ResourceManagerRef     resourceManager) const noexcept
	{
		return core::SharedRef<CommandList>::Make(
			desc,
			std::move(commandAllocator),
			std::move(resourceManager));
	}

	ResourceManagerRef
	Device::CreateResourceManager(const ResourceManagerDesc& desc) const noexcept
	{
		return core::SharedRef<ResourceManager>::Make(m_Device, desc);
	}

	RenderTargetRef
	Device::CreateRenderTarget(
		const RenderTargetDesc&           desc,
		core::SharedRef<ICommandQueue>    queue,
		core::SharedRef<IResourceManager> resourceManager,
		bool                              enableDebug) const
	{
		return core::SharedRef<RenderTarget>::Make(
			desc,
			DeviceRef(const_cast<Device*>(this)),
			std::move(queue),
			std::move(resourceManager),
			enableDebug);
	}

	ShaderRef
	Device::CreateShader(ShaderDesc desc) const noexcept
	{
		return core::SharedRef<Shader>::Make(std::move(desc), m_Context);
	}

	MeshletPipelineRef
	Device::CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept
	{
		return core::SharedRef<MeshletPipeline>::Make(m_Device.Get(), m_ShaderCache.get(), desc);
	}

	ComputePipelineRef
	Device::CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept
	{
		return core::SharedRef<ComputePipeline>::Make(m_Device.Get(), m_ShaderCache.get(), desc);
	}

	CommandAllocatorRef
	Device::CreateCommandAllocator(QueueType type) const noexcept
	{
		auto d3d12CmdAllocator = wrl::ComPtr<ID3D12CommandAllocator>();

		m_Device->CreateCommandAllocator(
			ConvertQueueType(type),
			IID_PPV_ARGS(&d3d12CmdAllocator)) >>
			d3d12ErrChecker;

		return core::SharedRef<CommandAllocator>::Make(std::move(d3d12CmdAllocator));
	}

	CommandQueueRef
	Device::CreateCommandQueue(QueueType type) const noexcept
	{
		return core::SharedRef<CommandQueue>::Make(type, m_Device.Get());
	}

	core::SharedRef<ITimestampHeap>
	Device::CreateTimestampHeap(uint32_t capacity) const noexcept
	{
		return core::SharedRef<TimestampHeap>::Make(m_Device.Get(), capacity);
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
}
