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
			wrl::ComPtr<ID3D12Device> device,
			const std::string&        shaderCacheDir,
			bool                      gpuValidation);

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
		CreateResourceManager(const ResourceManagerDesc& desc) const noexcept override;

		RenderTargetRef
		CreateRenderTarget(
			const RenderTargetDesc&           desc,
			core::SharedRef<ICommandQueue>    queue,
			core::SharedRef<IResourceManager> resourceManager,
			bool                              enableDebug) const override;

		// Without this the override hides IDevice's (module, entry) overload from callers holding a
		// concrete Device rather than an IDevice.
		using IDevice::CreateShader;

		core::SharedRef<IShader>
		CreateShader(ShaderDesc desc) const noexcept override;

		core::SharedRef<ICommandAllocator>
		CreateCommandAllocator(QueueType type) const noexcept override;

		core::SharedRef<ICommandQueue>
		CreateCommandQueue(QueueType type) const noexcept override;

		core::SharedRef<IMeshletPipeline>
		CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept override;

		[[nodiscard]] core::SharedRef<IGraphicsPipeline>
		CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) const noexcept override;

		core::SharedRef<IComputePipeline>
		CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept override;

		Uniforms
		CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

		Uniforms
		CreateUniforms(IGraphicsPipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

		[[nodiscard]] Uniforms
		CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

		/**
		 * The Slang session shaders are compiled through, created on first call.
		 *
		 * Creating it loads Slang's core module, which costs hundreds of megabytes that then stay
		 * resident, so only a shader-cache miss should ever reach this.
		 */
		slang::ISession*
		GetSlangSession() const noexcept;

		/**
		 * Releases the Slang sessions and every module they parsed. A later compile recreates
		 * them, so this only reclaims memory -- it does not disable compilation.
		 *
		 * @pre no slang::IModule or IComponentType obtained from this device is still held: they
		 *      keep the session alive, and any raw pointer to one dangles once it is dropped.
		 */
		void
		ReleaseSlangSession() noexcept;

	private:
		wrl::ComPtr<ID3D12Device> m_Device;

		// m_SlangGlobalSession must be declared before m_SlangSession so it is destroyed
		// after it
		mutable Slang::ComPtr<slang::IGlobalSession> m_SlangGlobalSession;
		mutable Slang::ComPtr<slang::ISession>       m_SlangSession;

		std::unique_ptr<ShaderCache> m_ShaderCache;
	};
}
