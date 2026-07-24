#pragma once

namespace bgl::wgpu
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

	/**
	 * Owns the WebGPU object stack -- instance, adapter, device and its default queue -- and
	 * routes the device's uncaptured errors and lost-device notification into the bgl log.
	 *
	 * Adapter and device requests are asynchronous in WebGPU. Natively they are resolved here by
	 * blocking on the returned future, which is what wgpuInstanceWaitAny permits and the browser
	 * does not; a browser build must drive these off the event loop instead.
	 */
	class Device
	{
	public:
		/** @throws GraphicsError if no adapter or device could be acquired. */
		explicit Device(const DeviceDesc& desc);

		~Device();

		Device(const Device&) = delete;
		Device&
		operator=(const Device&) = delete;
		Device(Device&&)         = delete;
		Device&
		operator=(Device&&) = delete;

		const AdapterInfo&
		GetAdapterInfo() const noexcept
		{
			return m_AdapterInfo;
		}

		WGPUDevice
		GetHandle() const noexcept
		{
			return m_Device;
		}

		WGPUQueue
		GetQueue() const noexcept
		{
			return m_Queue;
		}

		// Awaiting a future needs the instance: it is what runs the callbacks.
		WGPUInstance
		GetInstance() const noexcept
		{
			return m_Instance;
		}

	private:
		WGPUInstance m_Instance = nullptr;
		WGPUAdapter  m_Adapter  = nullptr;
		WGPUDevice   m_Device   = nullptr;
		WGPUQueue    m_Queue    = nullptr;

		AdapterInfo m_AdapterInfo;
	};
}
