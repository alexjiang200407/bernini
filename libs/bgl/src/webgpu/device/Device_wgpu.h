#pragma once

#include "device/Device.h"

namespace bgl
{
	namespace wgpu
	{
		struct AdapterInfo
		{
			std::string     vendor;
			std::string     architecture;
			std::string     device;
			std::string     description;
			WGPUBackendType backendType = WGPUBackendType_Undefined;
			WGPUAdapterType adapterType = WGPUAdapterType_Unknown;
		};

		struct DeviceDesc
		{
			WGPUPowerPreference powerPreference = WGPUPowerPreference_HighPerformance;
		};
	}

	/**
	 * The RHI device, and the owner of the WebGPU object stack it is built on -- instance,
	 * adapter, device and its default queue. Uncaptured errors and device loss are routed into
	 * the bgl log.
	 *
	 * Adapter and device requests are asynchronous in WebGPU. Natively they are resolved here by
	 * blocking on the returned future, which is what wgpuInstanceWaitAny permits and the browser
	 * does not; a browser build must drive these off the event loop instead.
	 *
	 * The factories that need a shader compiler, a mesh shader or a swapchain are not implemented
	 * yet and fail loudly rather than returning something unusable.
	 */
	class Device final : public core::RefCounter<IDevice>
	{
	public:
		/** @throws GraphicsError if no adapter or device could be acquired. */
		explicit Device(const wgpu::DeviceDesc& desc);

		~Device() noexcept override;

		Device(const Device&) noexcept = delete;
		Device(Device&&) noexcept      = delete;

		Device&
		operator=(const Device&) noexcept = delete;

		Device&
		operator=(Device&&) noexcept = delete;

		[[nodiscard]] const wgpu::AdapterInfo&
		GetAdapterInfo() const noexcept
		{
			return m_AdapterInfo;
		}

		[[nodiscard]] WGPUDevice
		GetHandle() const noexcept
		{
			return m_Device;
		}

		[[nodiscard]] WGPUQueue
		GetQueue() const noexcept
		{
			return m_Queue;
		}

		// Awaiting a future needs the instance: it is what runs the callbacks.
		[[nodiscard]] WGPUInstance
		GetInstance() const noexcept
		{
			return m_Instance;
		}

		[[nodiscard]] core::SharedRef<IShader>
		CreateShader(ShaderDesc desc) const noexcept override;

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

		[[nodiscard]] core::SharedRef<IResourceManager>
		CreateResourceManager(const ResourceManagerDesc& desc) const noexcept override;

		[[nodiscard]] RenderTargetRef
		CreateRenderTarget(
			const RenderTargetDesc&           desc,
			core::SharedRef<ICommandQueue>    queue,
			core::SharedRef<IResourceManager> resourceManager,
			bool                              enableDebug) const override;

		[[nodiscard]] Uniforms
		CreateUniforms(IMeshletPipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

		[[nodiscard]] Uniforms
		CreateUniforms(IComputePipeline const* pipeline, const std::string& cbufferName)
			const noexcept override;

	private:
		WGPUInstance m_Instance = nullptr;
		WGPUAdapter  m_Adapter  = nullptr;
		WGPUDevice   m_Device   = nullptr;
		WGPUQueue    m_Queue    = nullptr;

		wgpu::AdapterInfo m_AdapterInfo;
	};
}
