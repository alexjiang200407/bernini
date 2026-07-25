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

		ReadbackBuffer(WGPUDevice device, WGPUInstance instance, const ReadbackBufferDesc& desc);

		~ReadbackBuffer() noexcept;

		ReadbackBuffer(const ReadbackBuffer&) = delete;
		ReadbackBuffer(ReadbackBuffer&& other) noexcept;

		ReadbackBuffer&
		operator=(const ReadbackBuffer&) = delete;

		ReadbackBuffer&
		operator=(ReadbackBuffer&& other) noexcept;

		/** Blocks until the mapping completes. Idempotent; null if the map failed. */
		[[nodiscard]] const void*
		Map() noexcept;

		void
		Unmap() noexcept;

		[[nodiscard]] WGPUBuffer
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
		WGPUBuffer   m_Buffer   = nullptr;
		WGPUInstance m_Instance = nullptr;
		uint64_t     m_ByteSize = 0;
		const void*  m_Mapped   = nullptr;
	};
}
