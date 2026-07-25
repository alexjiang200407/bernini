#include "resource/Buffer_wgpu.h"

#include <core/math.h>

namespace bgl
{
	Buffer::Buffer(WGPUDevice device, const BufferDesc& desc) : m_Desc(desc)
	{
		gassert(device != nullptr, "Buffer: null device");
		gassert(desc.byteSize > 0, "Buffer '{}': zero byte size", desc.debugName);

		auto wgpuDesc  = WGPUBufferDescriptor{};
		wgpuDesc.label = wgpu::ToStringView(desc.debugName);
		// WebGPU rejects a storage binding whose size is not a multiple of 4.
		wgpuDesc.size = core::align(desc.byteSize, 4);
		// isUav does not change the usage: a WebGPU storage buffer binds read or read_write alike,
		// and the read-only-vs-UAV split lives in the bind group layout, not here.
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
		m_Desc(std::exchange(other.m_Desc, BufferDesc{}))
	{}

	Buffer&
	Buffer::operator=(Buffer&& other) noexcept
	{
		if (this != &other)
		{
			if (m_Buffer != nullptr)
				wgpuBufferRelease(m_Buffer);

			m_Buffer = std::exchange(other.m_Buffer, nullptr);
			m_Desc   = std::exchange(other.m_Desc, BufferDesc{});
		}

		return *this;
	}
}
