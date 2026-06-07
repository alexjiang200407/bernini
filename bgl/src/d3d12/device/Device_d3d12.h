#pragma once
#include "cmd/CommandAllocator.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "device/Device.h"
#include "pipeline/GraphicsPipeline.h"
#include "resource/ResourceManager.h"
#include "resource/Shader.h"
#include "types/QueueType.h"

namespace bgl
{
	class ShaderDesc;

	class Device final : public core::RefCounter<IDevice>
	{
	public:
		Device(wrl::ComPtr<ID3D12Device> device);

		CommandListHandle
		CreateCommandList(
			QueueType              type,
			CommandAllocatorHandle commandAllocator,
			ResourceManagerHandle  resourceManager) const override;

		ResourceManagerHandle
		CreateResourceManager(uint32_t maxCbvSrvUav, uint32_t maxRtvs) const override;

		ShaderHandle
		CreateShader(const ShaderDesc& desc) const override;

		ShaderHandle
		CreateShader(ShaderDesc&& desc) const override;

		CommandAllocatorHandle
		CreateCommandAllocator() const override;

		CommandQueueHandle
		CreateCommandQueue(QueueType type) const override;

		GraphicsPipelineHandle
		CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) const override;

	private:
		wrl::ComPtr<ID3D12Device> m_Device;
	};
}
