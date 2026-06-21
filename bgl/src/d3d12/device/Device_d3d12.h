#pragma once
#include "device/Device.h"

namespace bgl
{
	struct ShaderDesc;

	class Device final : public core::RefCounter<IDevice>
	{
	public:
		Device(wrl::ComPtr<ID3D12Device> device, slang::IGlobalSession* globalSession);
		~Device() noexcept override { logger::trace("~Device"); }
		Device(const Device&) noexcept = delete;
		Device(Device&&) noexcept      = delete;

		Device&
		operator=(const Device&) noexcept = delete;

		Device&
		operator=(Device&&) noexcept = delete;

		core::SharedRef<ICommandList>
		CreateCommandList(
			const CommandListDesc&             desc,
			core::SharedRef<ICommandAllocator> commandAllocator,
			core::SharedRef<IResourceManager>  resourceManager) const override;

		core::SharedRef<IResourceManager>
		CreateResourceManager(const ResourceManagerDesc& desc) const override;

		core::SharedRef<IShader>
		CreateShader(ShaderDesc desc) const override;

		core::SharedRef<ICommandAllocator>
		CreateCommandAllocator() const override;

		core::SharedRef<ICommandQueue>
		CreateCommandQueue(QueueType type) const override;

		core::SharedRef<IMeshletPipeline>
		CreateMeshletPipeline(const MeshletPipelineDesc& desc) const override;

		Uniforms
		CreateUniforms(IMeshletPipeline const* pipeline) const override;

	private:
		wrl::ComPtr<ID3D12Device>      m_Device;
		Slang::ComPtr<slang::ISession> m_SlangSession;
	};
}
