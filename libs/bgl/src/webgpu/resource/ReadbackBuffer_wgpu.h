#pragma once

#include "resource/Readback.h"

namespace bgl
{
	/**
	 * A CPU-readable copy destination.
	 *
	 * WebGPU maps buffers asynchronously, but IResourceManager::MapReadback is synchronous, so
	 * Map blocks on the map future. That is only legal off the browser's main thread -- a
	 * browser build has to hand the caller a future instead.
	 */
	class ReadbackBuffer final
	{
	public:
		ReadbackBuffer() = default;

		ReadbackBuffer(
			const wgpu::Device&       device,
			const wgpu::Instance&     instance,
			const ReadbackBufferDesc& desc);

		ReadbackBuffer(const ReadbackBuffer&)     = delete;
		ReadbackBuffer(ReadbackBuffer&&) noexcept = default;
		ReadbackBuffer&
		operator=(const ReadbackBuffer&) = delete;
		ReadbackBuffer&
		operator=(ReadbackBuffer&&) noexcept = default;

		/** Blocks until the mapping completes. Idempotent; null if the map failed. */
		[[nodiscard]] const void*
		Map() noexcept;

		void
		Unmap() noexcept;

		[[nodiscard]] const wgpu::Buffer&
		GetHandle() const noexcept
		{
			return m_Buffer;
		}

		[[nodiscard]] uint64_t
		GetByteSize() const noexcept
		{
			return m_ByteSize;
		}

	private:
		wgpu::Buffer   m_Buffer;
		wgpu::Instance m_Instance;
		uint64_t       m_ByteSize = 0;
		const void*    m_Mapped   = nullptr;
	};
}
