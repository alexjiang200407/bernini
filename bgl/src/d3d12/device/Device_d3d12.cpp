#pragma once
#include "device/Device_d3d12.h"
#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandList_d3d12.h"
#include "cmd/CommandQueue_d3d12.h"
#include "device/Device.h"
#include "pipeline/GraphicsPipeline_d3d12.h"
#include "resource/ResourceManager_d3d12.h"
#include "resource/Shader_d3d12.h"
#include <core/ref/SharedRef.h>

namespace bgl
{
	Device::Device(wrl::ComPtr<ID3D12Device> device) : m_Device(std::move(device)) {}

	CommandListHandle
	Device::CreateCommandList(
		QueueType              type,
		CommandAllocatorHandle commandAllocator,
		ResourceManagerHandle  resourceManager) const
	{
		return core::SharedRef<CommandList>::Make(
			type,
			std::move(commandAllocator),
			std::move(resourceManager));
	}

	ResourceManagerHandle
	Device::CreateResourceManager(uint32_t maxCbvSrvUav, uint32_t maxRtvs) const
	{
		return core::SharedRef<ResourceManager>::Make(m_Device, maxCbvSrvUav, maxRtvs);
	}

	ShaderHandle
	Device::CreateShader(const ShaderDesc& desc) const
	{
		return core::SharedRef<Shader>::Make(desc);
	}

	ShaderHandle
	Device::CreateShader(ShaderDesc&& desc) const
	{
		return core::SharedRef<Shader>::Make(std::move(desc));
	}

	GraphicsPipelineHandle
	Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) const
	{
		return core::SharedRef<GraphicsPipeline>::Make(m_Device.Get(), desc);
	}

	CommandAllocatorHandle
	Device::CreateCommandAllocator() const
	{
		auto d3d12CmdAllocator = wrl::ComPtr<ID3D12CommandAllocator>();

		m_Device->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(&d3d12CmdAllocator)) >>
			d3d12ErrChecker;

		return core::SharedRef<CommandAllocator>::Make(std::move(d3d12CmdAllocator));
	}

	CommandQueueHandle
	Device::CreateCommandQueue(QueueType type) const
	{
		return core::SharedRef<CommandQueue>::Make(type, m_Device.Get());
	}
}
