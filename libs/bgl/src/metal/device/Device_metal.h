#pragma once
#include "metal_cpp.h"

#include "device/Device.h"

#include <core/ref/RefCounter.h>

namespace bgl
{
	/**
	 * The RHI device over an MTL::Device -- the sole factory for queues, allocators, and (later)
	 * resources and pipelines. Only the command-submission factories are live in this slice; the
	 * resource/pipeline factories arrive with those objects.
	 */
	class Device final : public core::RefCounter<IDevice>
	{
	public:
		explicit Device(MTL::Device* device);

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
		CreateResourceManager(
			const ResourceManagerDesc&     desc,
			core::SharedRef<ICommandQueue> submissionQueue) const noexcept override;

		core::SharedRef<IShader>
		CreateShader(ShaderDesc desc) const noexcept override;

		core::SharedRef<IComputePipeline>
		CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept override;

		core::SharedRef<IMeshletPipeline>
		CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept override;

		Uniforms
		CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

		Uniforms
		CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

	private:
		NS::SharedPtr<MTL::Device>           m_Device;
		Slang::ComPtr<slang::IGlobalSession> m_SlangGlobalSession;
		Slang::ComPtr<slang::ISession>       m_SlangSession;
	};
}
