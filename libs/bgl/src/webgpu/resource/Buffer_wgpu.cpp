#include "resource/Buffer_wgpu.h"

#include <core/math.h>

namespace bgl
{
	Buffer::Buffer(const wgpu::Device& device, const BufferDesc& desc) : m_Desc(desc)
	{
		gassert(device != nullptr, "Buffer: null device");
		gassert(desc.byteSize > 0, "Buffer '{}': zero byte size", desc.debugName);

		auto wgpuDesc  = wgpu::BufferDescriptor{};
		wgpuDesc.label = std::string_view(desc.debugName);
		// WebGPU rejects a storage binding whose size is not a multiple of 4.
		wgpuDesc.size = core::align(desc.byteSize, 4);
		// isUav does not change the usage: a WebGPU storage buffer binds read or read_write alike,
		// and the read-only-vs-UAV split lives in the bind group layout, not here. Indirect is
		// granted to every buffer: the culling chain computes draw args into the same storage buffers
		// it later hands to drawIndirect, so there is no separate indirect-args buffer class.
		wgpuDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc |
		                 wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Indirect;

		m_Buffer = device.CreateBuffer(&wgpuDesc);
	}
}
