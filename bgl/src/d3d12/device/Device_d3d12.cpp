#pragma once
#include "device/Device_d3d12.h"
#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandList_d3d12.h"
#include "cmd/CommandQueue_d3d12.h"
#include "device/Device.h"
#include "pipeline/GraphicsPipeline_d3d12.h"
#include "resource/ResourceManager_d3d12.h"
#include "resource/Shader_d3d12.h"

namespace bgl
{
	DeviceImpl::DeviceImpl(wrl::ComPtr<ID3D12Device> device) : m_Device(std::move(device)) {}

	CommandList
	DeviceImpl::CreateCommandList(
		QueueType        type,
		CommandAllocator commandAllocator,
		ResourceManager  resourceManager) const
	{
		gassert(commandAllocator.IsInitialized(), "Command List not initialized");
		gassert(resourceManager.IsInitialized(), "Resource Manager not initialized");

		auto commandList = CommandList();
		commandList.EmplaceImpl(type, commandAllocator, resourceManager);
		return commandList;
	}

	ResourceManager
	DeviceImpl::CreateResourceManager(uint32_t maxCbvSrvUav, uint32_t maxRtvs) const
	{
		auto resourceManager = ResourceManager();
		resourceManager.EmplaceImpl(m_Device, maxCbvSrvUav, maxRtvs);
		return resourceManager;
	}

	Shader
	DeviceImpl::CreateShader(const ShaderDesc& desc) const
	{
		auto shader = Shader();
		shader.EmplaceImpl(desc);
		return shader;
	}

	Shader
	DeviceImpl::CreateShader(ShaderDesc&& desc) const
	{
		auto shader = Shader();
		shader.EmplaceImpl(std::move(desc));
		return shader;
	}

	GraphicsPipeline
	DeviceImpl::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) const
	{
		auto pipeline = GraphicsPipeline();
		pipeline.EmplaceImpl(m_Device.Get(), desc);
		return pipeline;
	}

	CommandAllocator
	DeviceImpl::CreateCommandAllocator() const
	{
		auto cmdAllocator      = CommandAllocator();
		auto d3d12CmdAllocator = wrl::ComPtr<ID3D12CommandAllocator>();

		m_Device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&d3d12CmdAllocator)) >>
			d3d12ErrChecker;

		cmdAllocator.EmplaceImpl(std::move(d3d12CmdAllocator));
		return cmdAllocator;
	}

	CommandQueue
	DeviceImpl::CreateCommandQueue(QueueType type) const
	{
		auto cmdQueue = CommandQueue();
		cmdQueue.EmplaceImpl(type, m_Device.Get());
		return cmdQueue;
	}

	CommandList
	Device::CreateGraphicsCommandList(
		CommandAllocator commandAllocator,
		ResourceManager  resourceManager) const
	{
		gassert(IsInitialized(), "Device not initialized");

		return GetImpl()->CreateCommandList(
			QueueType::kGraphics,
			commandAllocator,
			resourceManager);
	}

	CommandQueue
	Device::CreateGraphicsCommandQueue(QueueType type) const
	{
		gassert(IsInitialized(), "Device not initialized");
		return GetImpl()->CreateCommandQueue(QueueType::kGraphics);
	}

	CommandAllocator
	Device::CreateCommandAllocator() const
	{
		gassert(IsInitialized(), "Device not initialized");
		return GetImpl()->CreateCommandAllocator();
	}

	CommandQueue
	Device::CreateCommandQueue(QueueType type) const
	{
		gassert(IsInitialized(), "Device not initialized");
		return GetImpl()->CreateCommandQueue(type);
	}

	ResourceManager
	Device::CreateResourceManager(uint32_t maxCbvSrvUav, uint32_t maxRtvs) const
	{
		gassert(IsInitialized(), "Device not initialized");
		return GetImpl()->CreateResourceManager(maxCbvSrvUav, maxRtvs);
	}

	Shader
	Device::CreateShader(const ShaderDesc& desc) const
	{
		gassert(IsInitialized(), "Device not initialized");
		return GetImpl()->CreateShader(desc);
	}

	Shader
	bgl::Device::CreateShader(ShaderDesc&& desc) const
	{
		gassert(IsInitialized(), "Device not initialized");
		return GetImpl()->CreateShader(std::move(desc));
	}

	GraphicsPipeline
	Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) const
	{
		gassert(IsInitialized(), "Device not initialized");
		return GetImpl()->CreateGraphicsPipeline(desc);
	}
}
