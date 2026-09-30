#pragma once
#include "metal_cpp.h"
#include <bgl/IRenderTarget.h>
#include <core/ref/SharedRef.h>

#include <bgpu/GpuContext.h>
#include <bgpu/device/Device.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/uniforms/Uniforms.h>

#include <core/ref/RefCounter.h>
#include <cstdint>
#include <filesystem>
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
		/**
		 * The RHI device over the context's Metal device, compiling through the context's sessions
		 * and caching in its program cache, when it has one.
		 */
		explicit Device(const bgpu::GpuContextRef& context);

		// Out of line: m_ShaderCache holds an incomplete type here.
		~Device() override;

		/** Drops every thread's Slang session; the context's ReleaseSlangSessions is the contract. */
		void
		ReleaseSlangSession() noexcept override;

		[[nodiscard]] bgpu::GpuContext&
		GetGpuContext() const noexcept override
		{
			return *m_Context;
		}

		void
		AddSourceModule(const bgpu::SlangSourceModule& sourceModule) noexcept override;

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
		bgpu::GpuContextRef          m_Context;
		NS::SharedPtr<MTL::Device>   m_Device;
		std::unique_ptr<ShaderCache> m_ShaderCache;
	};
}
