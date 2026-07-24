#include "resource/Buffer_wgpu.h"

namespace bgl
{
	Buffer::Buffer(WGPUDevice device, const BufferDesc& desc) : m_ByteSize(desc.byteSize)
	{
		gassert(device != nullptr, "Buffer: null device");
		gassert(desc.byteSize > 0, "Buffer '{}': zero byte size", desc.debugName);

		auto wgpuDesc  = WGPUBufferDescriptor{};
		wgpuDesc.label = wgpu::ToStringView(desc.debugName);
		// Rounded up because WebGPU rejects a storage binding whose size is not a multiple of 4.
		wgpuDesc.size = (desc.byteSize + 3) & ~uint64_t{ 3 };
		wgpuDesc.usage =
			WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;

		m_Buffer = wgpuDeviceCreateBuffer(device, &wgpuDesc);
	}

	Buffer::~Buffer() noexcept
	{
		if (m_Buffer != nullptr)
			wgpuBufferRelease(m_Buffer);
	}

	Buffer::Buffer(Buffer&& other) noexcept :
		m_Buffer(std::exchange(other.m_Buffer, nullptr)),
		m_ByteSize(std::exchange(other.m_ByteSize, 0))
	{}

	Buffer&
	Buffer::operator=(Buffer&& other) noexcept
	{
		if (this != &other)
		{
			if (m_Buffer != nullptr)
				wgpuBufferRelease(m_Buffer);

			m_Buffer   = std::exchange(other.m_Buffer, nullptr);
			m_ByteSize = std::exchange(other.m_ByteSize, 0);
		}

		return *this;
	}
}
