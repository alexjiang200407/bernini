#pragma once
#include "device/Device.h"
#include <bgpu/GpuContext.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace bgl
{
	struct ShaderDesc;
	class ShaderCache;
	class ITimestampHeap;

	class Device final : public core::RefCounter<IDevice>
	{
	public:
		/**
		 * The RHI device over the context's D3D12 device, compiling through the context's sessions.
		 * `shaderCacheDir` empty disables the cache.
		 */
		Device(bgpu::GpuContextRef context, const std::filesystem::path& shaderCacheDir);

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

		core::SharedRef<IShader>
		CreateShader(ShaderDesc desc) const noexcept override;

		void
		AddSourceModule(const bgpu::SlangSourceModule& sourceModule) noexcept override;

		[[nodiscard]] std::optional<ReflectedSurface>
		ReflectSurfaceModule(std::string_view moduleName, std::string_view surfaceName) override;

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
		GetContext() const noexcept
		{
			return *m_Context;
		}

	private:
		// Declared first: the device below is the context's, and the cache is built on both.
		bgpu::GpuContextRef          m_Context;
		wrl::ComPtr<ID3D12Device>    m_Device;
		std::unique_ptr<ShaderCache> m_ShaderCache;
	};
}
