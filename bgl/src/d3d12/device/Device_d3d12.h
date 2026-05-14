#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "pipeline/GraphicsPipeline.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"
#include "types/QueueType.h"

namespace bgl
{
	class ShaderDesc;
	class DeviceImpl
	{
	public:
		DeviceImpl(wrl::ComPtr<ID3D12Device> device);

		CommandList
		CreateCommandList(
			QueueType        type,
			CommandAllocator commandAllocator,
			ResourceManager  resourceManager) const;

		ResourceManager
		CreateResourceManager(uint32_t maxCbvSrvUav, uint32_t maxRtvs) const;

		Shader
		CreateShader(const ShaderDesc& desc) const;

		Shader
		CreateShader(ShaderDesc&& desc) const;

		GraphicsPipeline
		CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) const;

		CommandAllocator
		CreateCommandAllocator() const;

		CommandQueue
		CreateCommandQueue(QueueType type) const;

	private:
		wrl::ComPtr<ID3D12Device> m_Device;

		friend class Device;
	};
}
