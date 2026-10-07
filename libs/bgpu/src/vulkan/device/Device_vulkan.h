#pragma once
#include "shadercache/ShaderCache_vulkan.h"
#include <bgpu/GpuContext.h>
#include <bgpu/device/Device.h>
#include <bgpu/types/QueueType.h>
#include <bgpu/uniforms/Uniforms.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <memory>
#include <string>

namespace bgpu
{
	/**
	 * The RHI device over the context's Vulkan device: the compute half. Buffers, queues, command
	 * lists and compute pipelines; textures and the meshlet pipeline end the process naming what is
	 * missing.
	 */
	class Device final : public core::RefCounter<IDevice>
	{
	public:
		explicit Device(GpuContextRef context);
		~Device() noexcept override;

		Device(const Device&) = delete;
		Device(Device&&)      = delete;
		Device&
		operator=(const Device&) = delete;
		Device&
		operator=(Device&&) = delete;

		[[nodiscard]] core::SharedRef<IShader>
		CreateShader(ShaderDesc desc) const noexcept override;

		void
		AddSourceModule(const SlangSourceModule& sourceModule) noexcept override;

		void
		ReleaseSlangSession() noexcept override;

		[[nodiscard]] GpuContext&
		GetGpuContext() const noexcept override
		{
			return *m_Context;
		}

		[[nodiscard]] core::SharedRef<IComputePipeline>
		CreateComputePipeline(const ComputePipelineDesc& desc) const noexcept override;

		[[nodiscard]] core::SharedRef<IMeshletPipeline>
		CreateMeshletPipeline(const MeshletPipelineDesc& desc) const noexcept override;

		core::SharedRef<ICommandList>
		CreateCommandList(
			const CommandListDesc&             desc,
			core::SharedRef<ICommandAllocator> commandAllocator,
			core::SharedRef<IResourceManager>  resourceManager) const noexcept override;

		[[nodiscard]] core::SharedRef<ICommandAllocator>
		CreateCommandAllocator(QueueType type) const noexcept override;

		[[nodiscard]] core::SharedRef<ICommandQueue>
		CreateCommandQueue(QueueType type) const noexcept override;

		[[nodiscard]] core::SharedRef<ITimestampHeap>
		CreateTimestampHeap(uint32_t capacity) const noexcept override;

		[[nodiscard]] core::SharedRef<IResourceManager>
		CreateResourceManager(const ResourceManagerDesc& desc) const noexcept override;

		[[nodiscard]] Uniforms
		CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

		[[nodiscard]] Uniforms
		CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

	private:
		GpuContextRef                m_Context;
		std::unique_ptr<ShaderCache> m_ShaderCache;
	};
}
