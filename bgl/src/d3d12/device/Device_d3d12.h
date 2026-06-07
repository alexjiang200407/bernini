#pragma once
#include "device/Device.h"

namespace bgl
{
	class ShaderDesc;

	class Device final : public core::RefCounter<IDevice>
	{
	public:
		Device(wrl::ComPtr<ID3D12Device> device);

		core::SharedRef<ICommandList>
		CreateCommandList(
			const CommandListDesc&             desc,
			core::SharedRef<ICommandAllocator> commandAllocator,
			core::SharedRef<IResourceManager>  resourceManager) const override;

		core::SharedRef<IResourceManager>
		CreateResourceManager(uint32_t maxCbvSrvUav, uint32_t maxRtvs) const override;

		core::SharedRef<IShader>
		CreateShader(const ShaderDesc& desc) const override;

		core::SharedRef<IShader>
		CreateShader(ShaderDesc&& desc) const override;

		core::SharedRef<ICommandAllocator>
		CreateCommandAllocator() const override;

		core::SharedRef<ICommandQueue>
		CreateCommandQueue(QueueType type) const override;

		core::SharedRef<IGraphicsPipeline>
		CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) const override;

	private:
		wrl::ComPtr<ID3D12Device> m_Device;
	};
}
