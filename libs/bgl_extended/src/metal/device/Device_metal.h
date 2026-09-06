#pragma once
#include "metal_cpp.h"
#include <bgl/IRenderTarget.h>
#include <core/ref/SharedRef.h>

#include "device/Device.h"
#include "slang/SlangSessions.h"
#include "types/QueueType.h"
#include "uniforms/Uniforms.h"

#include <core/ref/RefCounter.h>
#include <cstdint>
#include <memory>
#include <string>

namespace bgl
{
	class ShaderCache;
	class ITimestampHeap;

	/**
	 * The RHI device over an MTL::Device -- the sole factory for queues, allocators, and (later)
	 * resources and pipelines. Only the command-submission factories are live in this slice; the
	 * resource/pipeline factories arrive with those objects.
	 */
	class Device final : public core::RefCounter<IDevice>
	{
	public:
		Device(MTL::Device* device, const std::string& shaderCacheDir, bool usePipelineLibrary);

		// Out of line: m_ShaderCache holds an incomplete type here.
		~Device() override;

		/** Drops every thread's Slang session; see SlangSessions::ReleaseAll for the contract. */
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

		core::SharedRef<ITimestampHeap>
		CreateTimestampHeap(uint32_t capacity) const noexcept override;

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

		Uniforms
		CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

		Uniforms
		CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

	private:
		NS::SharedPtr<MTL::Device> m_Device;
		// Shaders resolve their modules through it, and CreateShader is const.
		mutable SlangSessions        m_Slang;
		std::unique_ptr<ShaderCache> m_ShaderCache;
	};
}
