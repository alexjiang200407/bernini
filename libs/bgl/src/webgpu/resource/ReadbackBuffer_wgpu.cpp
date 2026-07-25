#include "resource/ReadbackBuffer_wgpu.h"

#include <core/math.h>

namespace bgl
{
	ReadbackBuffer::ReadbackBuffer(
		const wgpu::Device&       device,
		const wgpu::Instance&     instance,
		const ReadbackBufferDesc& desc) : m_Instance(instance), m_ByteSize(desc.byteSize)
	{
		gassert(device != nullptr, "ReadbackBuffer: null device");
		gassert(desc.byteSize > 0, "ReadbackBuffer '{}': zero byte size", desc.debugName);

		auto wgpuDesc  = wgpu::BufferDescriptor{};
		wgpuDesc.label = std::string_view(desc.debugName);
		wgpuDesc.size  = core::align(desc.byteSize, 4);
		wgpuDesc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;

		m_Buffer = device.CreateBuffer(&wgpuDesc);
	}

	const void*
	ReadbackBuffer::Map() noexcept
	{
		if (m_Mapped != nullptr)
			return m_Mapped;

		if (m_Buffer == nullptr)
			return nullptr;

		auto status = wgpu::MapAsyncStatus::Error;

		const auto future = m_Buffer.MapAsync(
			wgpu::MapMode::Read,
			0,
			wgpu::kWholeMapSize,
			wgpu::CallbackMode::WaitAnyOnly,
			[&status](wgpu::MapAsyncStatus s, wgpu::StringView message) {
				status = s;
				if (s != wgpu::MapAsyncStatus::Success)
					logger::error("[wgpu] readback map failed: {}", std::string_view(message));
			});

		m_Instance.WaitAny(future, UINT64_MAX);

		if (status != wgpu::MapAsyncStatus::Success)
			return nullptr;

		m_Mapped = m_Buffer.GetConstMappedRange(0, wgpu::kWholeMapSize);

		return m_Mapped;
	}

	void
	ReadbackBuffer::Unmap() noexcept
	{
		if (m_Mapped == nullptr)
			return;

		m_Buffer.Unmap();
		m_Mapped = nullptr;
	}
}
