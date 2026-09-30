#pragma once
#include <bgpu/GpuContext.h>
#include <bgpu/device/Device.h>
#include <bgpu/types/NativeObject.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace bgpu
{
	struct ShaderDesc;
	class ShaderCache;
	class ITimestampHeap;

	class Device final : public core::RefCounter<IDevice>
	{
	public:
		[[nodiscard]] NativeObject
		GetNativeObject(NativeObjectType type) const noexcept override
		{
			return type == NativeObjectType::kD3D12Device ? NativeObject{ m_Device.Get() } :
			                                                NativeObject{};
		}

		/**
		 * The RHI device over the context's D3D12 device, compiling through the context's sessions
		 * and caching in its program cache, when it has one.
		 */
		explicit Device(const bgpu::GpuContextRef& context);

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

		core::SharedRef<IShader>
		CreateShader(ShaderDesc desc) const noexcept override;

		void
		AddSourceModule(const bgpu::SlangSourceModule& sourceModule) noexcept override;

		core::SharedRef<ICommandAllocator>
		CreateCommandAllocator(QueueType type) const noexcept override;

		core::SharedRef<ICommandQueue>
		CreateCommandQueue(QueueType type) const noexcept override;

		core::SharedRef<ITimestampHeap>
		CreateTimestampHeap(uint32_t capacity) const noexcept override;

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

		/** Drops every thread's Slang session; the context's ReleaseSlangSessions is the contract. */
		void
		ReleaseSlangSession() noexcept override;

		[[nodiscard]] bgpu::GpuContext&
		GetGpuContext() const noexcept override
		{
			return *m_Context;
		}

	private:
		bgpu::GpuContextRef          m_Context;
		wrl::ComPtr<ID3D12Device>    m_Device;
		std::unique_ptr<ShaderCache> m_ShaderCache;
	};
}
