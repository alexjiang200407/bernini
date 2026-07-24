#include "device/Device_wgpu.h"

#include <bgl/IGraphics.h>

namespace bgl::wgpu
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
		RequestAdapter(WGPUInstance instance, const DeviceDesc& desc)
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
					out.message = ToString(message);
			};

			WaitFor(instance, wgpuInstanceRequestAdapter(instance, &opts, info));

			if (result.adapter == nullptr)
				throw GraphicsError("wgpu: no adapter available: " + result.message);

			return result.adapter;
		}

		AdapterInfo
		ReadAdapterInfo(WGPUAdapter adapter)
		{
			auto raw = WGPUAdapterInfo{};
			if (wgpuAdapterGetInfo(adapter, &raw) != WGPUStatus_Success)
				throw GraphicsError("wgpu: could not read adapter info");

			auto info         = AdapterInfo{};
			info.vendor       = ToString(raw.vendor);
			info.architecture = ToString(raw.architecture);
			info.device       = ToString(raw.device);
			info.description  = ToString(raw.description);
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
				ToString(message));
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
				ToString(message));
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
					out.message = ToString(message);
			};

			WaitFor(instance, wgpuAdapterRequestDevice(adapter, &deviceDesc, info));

			if (result.device == nullptr)
				throw GraphicsError("wgpu: could not create device: " + result.message);

			return result.device;
		}
	}

	Device::Device(const DeviceDesc& desc)
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

	Device::~Device()
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
}
