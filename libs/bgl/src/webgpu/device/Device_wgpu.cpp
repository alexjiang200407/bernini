#include "device/Device_wgpu.h"

#include "cmd/CommandAllocator_wgpu.h"
#include "cmd/CommandList_wgpu.h"
#include "cmd/CommandQueue_wgpu.h"
#include "resource/ResourceManager_wgpu.h"

#include <bgl/IGraphics.h>

namespace bgl
{
	namespace
	{
		void
		WaitFor(WGPUInstance instance, WGPUFuture future)
		{
			auto wait   = WGPUFutureWaitInfo{};
			wait.future = future;

			const auto status = wgpuInstanceWaitAny(instance, 1, &wait, UINT64_MAX);
			if (status != WGPUWaitStatus_Success)
				throw GraphicsError(
					"wgpu: waiting on a future failed with status " +
					std::to_string(static_cast<int>(status)));
		}

		WGPUAdapter
		RequestAdapter(WGPUInstance instance, const wgpu::DeviceDesc& desc)
		{
			auto opts            = WGPURequestAdapterOptions{};
			opts.powerPreference = desc.powerPreference;

			struct Result
			{
				WGPUAdapter adapter = nullptr;
				std::string message;
			} result;

			auto info      = WGPURequestAdapterCallbackInfo{};
			info.mode      = WGPUCallbackMode_WaitAnyOnly;
			info.userdata1 = &result;
			info.callback  = [](WGPURequestAdapterStatus status,
			                    WGPUAdapter              adapter,
			                    WGPUStringView           message,
			                    void*                    userdata,
			                    void*) {
				auto& out = *static_cast<Result*>(userdata);
				if (status == WGPURequestAdapterStatus_Success)
					out.adapter = adapter;
				else
					out.message = wgpu::ToString(message);
			};

			WaitFor(instance, wgpuInstanceRequestAdapter(instance, &opts, info));

			if (result.adapter == nullptr)
				throw GraphicsError("wgpu: no adapter available: " + result.message);

			return result.adapter;
		}

		wgpu::AdapterInfo
		ReadAdapterInfo(WGPUAdapter adapter)
		{
			auto raw = WGPUAdapterInfo{};
			if (wgpuAdapterGetInfo(adapter, &raw) != WGPUStatus_Success)
				throw GraphicsError("wgpu: could not read adapter info");

			auto info         = wgpu::AdapterInfo{};
			info.vendor       = wgpu::ToString(raw.vendor);
			info.architecture = wgpu::ToString(raw.architecture);
			info.device       = wgpu::ToString(raw.device);
			info.description  = wgpu::ToString(raw.description);
			info.backendType  = raw.backendType;
			info.adapterType  = raw.adapterType;

			wgpuAdapterInfoFreeMembers(raw);

			return info;
		}

		void
		OnUncapturedError(
			const WGPUDevice*,
			WGPUErrorType  type,
			WGPUStringView message,
			void*,
			void*)
		{
			logger::error(
				"[wgpu] uncaptured error ({}): {}",
				static_cast<int>(type),
				wgpu::ToString(message));
		}

		void
		OnDeviceLost(
			const WGPUDevice*,
			WGPUDeviceLostReason reason,
			WGPUStringView       message,
			void*,
			void*)
		{
			// Destroying the device reports a loss through this same callback; that one is expected.
			if (reason == WGPUDeviceLostReason_Destroyed)
				return;

			logger::error(
				"[wgpu] device lost ({}): {}",
				static_cast<int>(reason),
				wgpu::ToString(message));
		}

		WGPUDevice
		RequestDevice(WGPUInstance instance, WGPUAdapter adapter)
		{
			auto deviceDesc                        = WGPUDeviceDescriptor{};
			deviceDesc.uncapturedErrorCallbackInfo = { nullptr,
				                                       OnUncapturedError,
				                                       nullptr,
				                                       nullptr };
			deviceDesc.deviceLostCallbackInfo      = { nullptr,
				                                       WGPUCallbackMode_AllowSpontaneous,
				                                       OnDeviceLost,
				                                       nullptr,
				                                       nullptr };

			struct Result
			{
				WGPUDevice  device = nullptr;
				std::string message;
			} result;

			auto info      = WGPURequestDeviceCallbackInfo{};
			info.mode      = WGPUCallbackMode_WaitAnyOnly;
			info.userdata1 = &result;
			info.callback  = [](WGPURequestDeviceStatus status,
			                    WGPUDevice              device,
			                    WGPUStringView          message,
			                    void*                   userdata,
			                    void*) {
				auto& out = *static_cast<Result*>(userdata);
				if (status == WGPURequestDeviceStatus_Success)
					out.device = device;
				else
					out.message = wgpu::ToString(message);
			};

			WaitFor(instance, wgpuAdapterRequestDevice(adapter, &deviceDesc, info));

			if (result.device == nullptr)
				throw GraphicsError("wgpu: could not create device: " + result.message);

			return result.device;
		}
	}

	Device::Device(const wgpu::DeviceDesc& desc)
	{
		// Blocking on a future is opt-in: without TimedWaitAny, wgpuInstanceWaitAny rejects any
		// non-zero timeout. A browser has no such feature and must poll instead.
		constexpr auto features =
			std::array<WGPUInstanceFeatureName, 1>{ { WGPUInstanceFeatureName_TimedWaitAny } };

		auto instanceDesc                 = WGPUInstanceDescriptor{};
		instanceDesc.requiredFeatureCount = features.size();
		instanceDesc.requiredFeatures     = features.data();

		m_Instance = wgpuCreateInstance(&instanceDesc);
		if (m_Instance == nullptr)
			throw GraphicsError("wgpu: could not create instance");

		m_Adapter     = RequestAdapter(m_Instance, desc);
		m_AdapterInfo = ReadAdapterInfo(m_Adapter);
		m_Device      = RequestDevice(m_Instance, m_Adapter);
		m_Queue       = wgpuDeviceGetQueue(m_Device);

		logger::info(
			"[wgpu] adapter '{}' ({}), backend {}",
			m_AdapterInfo.device,
			m_AdapterInfo.description,
			static_cast<int>(m_AdapterInfo.backendType));
	}

	Device::~Device() noexcept
	{
		if (m_Queue != nullptr)
			wgpuQueueRelease(m_Queue);
		if (m_Device != nullptr)
			wgpuDeviceRelease(m_Device);
		if (m_Adapter != nullptr)
			wgpuAdapterRelease(m_Adapter);
		if (m_Instance != nullptr)
			wgpuInstanceRelease(m_Instance);
	}

	core::SharedRef<ICommandQueue>
	Device::CreateCommandQueue(QueueType) const noexcept
	{
		// WebGPU exposes one queue per device, so every type resolves to the same one; the
		// distinction only survives as the CommandListDesc::type a list records against.
		return core::SharedRef<CommandQueue>::Make(m_Instance, m_Queue);
	}

	core::SharedRef<ICommandAllocator>
	Device::CreateCommandAllocator(QueueType) const noexcept
	{
		return core::SharedRef<CommandAllocator>::Make();
	}

	core::SharedRef<ICommandList>
	Device::CreateCommandList(
		const CommandListDesc&             desc,
		core::SharedRef<ICommandAllocator> commandAllocator,
		core::SharedRef<IResourceManager>  resourceManager) const noexcept
	{
		return core::SharedRef<CommandList>::Make(m_Device, desc, std::move(resourceManager));
	}

	core::SharedRef<IResourceManager>
	Device::CreateResourceManager(const ResourceManagerDesc& desc) const noexcept
	{
		return core::SharedRef<ResourceManager>::Make(m_Device, m_Instance, desc);
	}

	// Unreachable until pipelines exist: there is no way to obtain the argument. The body is
	// backend-agnostic (it reads reflection off the pipeline) and lands with them.
	Uniforms
	Device::CreateUniforms(IMeshletPipeline const*, const std::string&) const noexcept
	{
		gfatal("CreateUniforms: WebGPU has no mesh shaders");
	}

	Uniforms
	Device::CreateUniforms(IComputePipeline const*, const std::string&) const noexcept
	{
		gfatal("CreateUniforms: compute pipelines are not implemented on the WebGPU backend yet");
	}

	core::SharedRef<IShader>
	Device::CreateShader(ShaderDesc) const noexcept
	{
		gfatal("CreateShader: the WebGPU backend has no Slang-to-WGSL path yet");
	}

	core::SharedRef<IComputePipeline>
	Device::CreateComputePipeline(const ComputePipelineDesc&) const noexcept
	{
		gfatal("CreateComputePipeline: not implemented on the WebGPU backend yet");
	}

	core::SharedRef<IMeshletPipeline>
	Device::CreateMeshletPipeline(const MeshletPipelineDesc&) const noexcept
	{
		gfatal("CreateMeshletPipeline: WebGPU has no mesh shaders");
	}

	RenderTargetRef
	Device::CreateRenderTarget(
		const RenderTargetDesc&,
		core::SharedRef<ICommandQueue>,
		core::SharedRef<IResourceManager>,
		bool) const
	{
		throw GraphicsError("CreateRenderTarget: not implemented on the WebGPU backend yet");
	}
}
