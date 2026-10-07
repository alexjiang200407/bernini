#pragma once
#include "volk_vulkan.h"
#include <bgpu/GpuContext.h>
#include <core/ref/Ref.h>
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <string_view>

namespace bgpu
{
	enum class BufferMemoryKind : uint8_t
	{
		// Device-local, written by copies and shaders, read as a storage buffer or a copy source.
		kDevice,
		// Host-visible and coherent, mapped for its whole life: the upload ring's chunks.
		kUpload,
		// Host-visible, the destination of GPU-to-CPU copies.
		kReadback,
	};

	/**
	 * A VkBuffer and the one allocation behind it, as D3D12 commits one resource per buffer.
	 *
	 * Ref-counted because Vulkan counts no references to a buffer and another owner on the same
	 * context may adopt it (ImportNativeBuffer): the memory lives until the last owner lets go. It
	 * holds the context, so the device outlives it.
	 */
	class BufferMemory final : public core::RefCounter<core::Ref>
	{
	public:
		/** @throws std::runtime_error when the buffer or its memory cannot be created. */
		BufferMemory(
			GpuContextRef    context,
			uint64_t         byteSize,
			BufferMemoryKind kind,
			std::string_view debugName);

		~BufferMemory() noexcept override;

		BufferMemory(const BufferMemory&) = delete;
		BufferMemory(BufferMemory&&)      = delete;
		BufferMemory&
		operator=(const BufferMemory&) = delete;
		BufferMemory&
		operator=(BufferMemory&&) = delete;

		/**
		 * The device buffer a bgpu manager on any context made as `buffer`, with a reference added;
		 * null for a buffer none made.
		 *
		 * @pre the owner of `buffer` keeps it alive for the duration of the call.
		 */
		[[nodiscard]] static core::SharedRef<BufferMemory>
		Find(VkBuffer buffer) noexcept;

		[[nodiscard]] VkBuffer
		GetVkBuffer() const noexcept
		{
			return m_Buffer;
		}

		[[nodiscard]] uint64_t
		GetByteSize() const noexcept
		{
			return m_ByteSize;
		}

		[[nodiscard]] const GpuContextRef&
		GetContext() const noexcept
		{
			return m_Context;
		}

		/** The mapping of a host-visible buffer, null for a device one. */
		[[nodiscard]] void*
		GetMapped() const noexcept
		{
			return m_Mapped;
		}

		/** Makes what the GPU wrote visible to a mapped read; a no-op on coherent memory. */
		void
		InvalidateForRead() const noexcept;

	private:
		void
		Destroy() noexcept;

		GpuContextRef    m_Context;
		VkDevice         m_Device   = VK_NULL_HANDLE;
		VkBuffer         m_Buffer   = VK_NULL_HANDLE;
		VkDeviceMemory   m_Memory   = VK_NULL_HANDLE;
		void*            m_Mapped   = nullptr;
		uint64_t         m_ByteSize = 0;
		BufferMemoryKind m_Kind     = BufferMemoryKind::kDevice;
		bool             m_Coherent = true;
	};
}
