#pragma once
#include "device/Device.h"

namespace bgl
{
	struct ShaderDesc;
	class ShaderCache;

	class Device final : public core::RefCounter<IDevice>
	{
	public:
		Device(
			wrl::ComPtr<ID3D12Device>            device,
			Slang::ComPtr<slang::IGlobalSession> globalSession,
			const std::string&                   shaderCacheDir,
			bool                                 gpuValidation);

		~Device() noexcept override;
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
			core::SharedRef<IResourceManager>  resourceManager) const noexcept override;

		core::SharedRef<IResourceManager>
		CreateResourceManager(
			const ResourceManagerDesc&     desc,
			core::SharedRef<ICommandQueue> submissionQueue) const noexcept override;

		core::SharedRef<IShader>
		CreateShader(ShaderDesc desc) const noexcept override;

		core::SharedRef<ICommandAllocator>
		CreateCommandAllocator(QueueType type) const noexcept override;

		core::SharedRef<ICommandQueue>
		CreateCommandQueue(QueueType type) const noexcept override;

		core::SharedRef<IMeshletPipeline>
		CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept override;

		core::SharedRef<IComputePipeline>
		CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept override;

		Uniforms
		CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

		Uniforms
		CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

	private:
		wrl::ComPtr<ID3D12Device> m_Device;

		// m_SlangGlobalSession must be declared before m_SlangSession so it is destroyed
		// after it
		Slang::ComPtr<slang::IGlobalSession> m_SlangGlobalSession;
		Slang::ComPtr<slang::ISession>       m_SlangSession;

		std::unique_ptr<ShaderCache> m_ShaderCache;
	};
}
