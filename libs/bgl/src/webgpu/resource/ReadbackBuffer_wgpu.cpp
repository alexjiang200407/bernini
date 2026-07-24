#include "resource/ReadbackBuffer_wgpu.h"

namespace bgl
{
	ReadbackBuffer::ReadbackBuffer(
		WGPUDevice                device,
		WGPUInstance              instance,
		const ReadbackBufferDesc& desc) : m_Instance(instance), m_ByteSize(desc.byteSize)
	{
		gassert(device != nullptr, "ReadbackBuffer: null device");
		gassert(desc.byteSize > 0, "ReadbackBuffer '{}': zero byte size", desc.debugName);

		wgpuInstanceAddRef(m_Instance);

		auto wgpuDesc  = WGPUBufferDescriptor{};
		wgpuDesc.label = wgpu::ToStringView(desc.debugName);
		wgpuDesc.size  = (desc.byteSize + 3) & ~uint64_t{ 3 };
		wgpuDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;

		m_Buffer = wgpuDeviceCreateBuffer(device, &wgpuDesc);
	}

	ReadbackBuffer::~ReadbackBuffer() noexcept
	{
		Unmap();

		if (m_Buffer != nullptr)
			wgpuBufferRelease(m_Buffer);

		if (m_Instance != nullptr)
			wgpuInstanceRelease(m_Instance);
	}

	ReadbackBuffer::ReadbackBuffer(ReadbackBuffer&& other) noexcept :
		m_Buffer(std::exchange(other.m_Buffer, nullptr)),
		m_Instance(std::exchange(other.m_Instance, nullptr)),
		m_ByteSize(std::exchange(other.m_ByteSize, 0)),
		m_Mapped(std::exchange(other.m_Mapped, nullptr))
	{}

	ReadbackBuffer&
	ReadbackBuffer::operator=(ReadbackBuffer&& other) noexcept
	{
		if (this != &other)
		{
			Unmap();

			if (m_Buffer != nullptr)
				wgpuBufferRelease(m_Buffer);
			if (m_Instance != nullptr)
				wgpuInstanceRelease(m_Instance);

			m_Buffer   = std::exchange(other.m_Buffer, nullptr);
			m_Instance = std::exchange(other.m_Instance, nullptr);
			m_ByteSize = std::exchange(other.m_ByteSize, 0);
			m_Mapped   = std::exchange(other.m_Mapped, nullptr);
		}

		return *this;
	}

	const void*
	ReadbackBuffer::Map() noexcept
	{
		if (m_Mapped != nullptr)
			return m_Mapped;

		if (m_Buffer == nullptr)
			return nullptr;

		auto status = WGPUMapAsyncStatus_Error;

		auto info      = WGPUBufferMapCallbackInfo{};
		info.mode      = WGPUCallbackMode_WaitAnyOnly;
		info.userdata1 = &status;
		info.callback  = [](WGPUMapAsyncStatus s, WGPUStringView message, void* userdata, void*) {
			*static_cast<WGPUMapAsyncStatus*>(userdata) = s;
			if (s != WGPUMapAsyncStatus_Success)
				logger::error("[wgpu] readback map failed: {}", wgpu::ToString(message));
		};

		auto wait   = WGPUFutureWaitInfo{};
		wait.future = wgpuBufferMapAsync(m_Buffer, WGPUMapMode_Read, 0, WGPU_WHOLE_MAP_SIZE, info);

		wgpuInstanceWaitAny(m_Instance, 1, &wait, UINT64_MAX);

		if (status != WGPUMapAsyncStatus_Success)
			return nullptr;

		m_Mapped = wgpuBufferGetConstMappedRange(m_Buffer, 0, WGPU_WHOLE_MAP_SIZE);

		return m_Mapped;
	}

	void
	ReadbackBuffer::Unmap() noexcept
	{
		if (m_Mapped == nullptr)
			return;

		wgpuBufferUnmap(m_Buffer);
		m_Mapped = nullptr;
	}
}
