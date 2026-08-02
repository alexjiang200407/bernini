#pragma once
#include "metal_cpp.h"

#include "device/Device.h"

#include <core/ref/RefCounter.h>

namespace bgl
{
	class ShaderCache;

	/**
	 * The RHI device over an MTL::Device -- the sole factory for queues, allocators, and (later)
	 * resources and pipelines. Only the command-submission factories are live in this slice; the
	 * resource/pipeline factories arrive with those objects.
	 */
	class Device final : public core::RefCounter<IDevice>
	{
	public:
		Device(MTL::Device* device, const std::string& shaderCacheDir);

		// Out of line: m_ShaderCache holds an incomplete type here.
		~Device() override;

		/**
		 * The Slang session, created on first use. Its core module is a few hundred megabytes
		 * resident, so it is not stood up until something actually compiles.
		 */
		[[nodiscard]] slang::ISession*
		GetSlangSession() const noexcept;

		/**
		 * Releases the Slang sessions and every module they parsed. A later compile recreates them,
		 * so this only reclaims memory -- it does not disable compilation.
		 *
		 * @pre no slang::IModule or IComponentType obtained from this device is still held: they
		 *      keep the session alive, and any raw pointer to one dangles once it is dropped.
		 */
		void
		ReleaseSlangSession() noexcept;

		[[nodiscard]] MTL::Device*
		GetMTLDevice() const noexcept
		{
			return m_Device.get();
		}

		core::SharedRef<ICommandQueue>
		CreateCommandQueue(QueueType type) const noexcept override;

		core::SharedRef<ICommandAllocator>
		CreateCommandAllocator(QueueType type) const noexcept override;

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

		core::SharedRef<IShader>
		CreateShader(ShaderDesc desc) const noexcept override;

		core::SharedRef<IComputePipeline>
		CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept override;

		core::SharedRef<IMeshletPipeline>
		CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept override;

		UniformsRef
		CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

		UniformsRef
		CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

	private:
		NS::SharedPtr<MTL::Device> m_Device;
		// m_SlangGlobalSession is declared before m_SlangSession so it is destroyed after it.
		mutable Slang::ComPtr<slang::IGlobalSession> m_SlangGlobalSession;
		mutable Slang::ComPtr<slang::ISession>       m_SlangSession;
		std::unique_ptr<ShaderCache>                 m_ShaderCache;
	};
}
