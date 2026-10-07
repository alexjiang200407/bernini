#pragma once
#include "resource/BufferMemory_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/GpuContext.h>
#include <bgpu/resource/Readback.h>
#include <core/err/util.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <utility>

namespace bgpu
{
	/** A host-visible buffer, the destination of GPU-to-CPU copies, mapped for its whole life. */
	class ReadbackBuffer final
	{
	public:
		ReadbackBuffer() = default;

		/** @throws std::runtime_error when the buffer or its memory cannot be created. */
		ReadbackBuffer(GpuContextRef context, const ReadbackBufferDesc& desc) :
			m_Memory(
				core::SharedRef<BufferMemory>::Make(
					std::move(context),
					desc.byteSize,
					BufferMemoryKind::kReadback,
					desc.debugName))
		{}

		~ReadbackBuffer() noexcept = default;

		ReadbackBuffer(const ReadbackBuffer&)     = delete;
		ReadbackBuffer(ReadbackBuffer&&) noexcept = default;
		ReadbackBuffer&
		operator=(const ReadbackBuffer&) = delete;
		ReadbackBuffer&
		operator=(ReadbackBuffer&&) noexcept = default;

		[[nodiscard]] VkBuffer
		GetVkBuffer() const noexcept
		{
			return m_Memory->GetVkBuffer();
		}

		[[nodiscard]] uint64_t
		GetByteSize() const noexcept
		{
			return m_Memory != nullptr ? m_Memory->GetByteSize() : 0;
		}

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_Memory == nullptr;
		}

		/** @pre the copy into the buffer has completed. */
		[[nodiscard]] const void*
		Map() const noexcept
		{
			core::ensure(m_Memory != nullptr, "Cannot map a null readback buffer");
			m_Memory->InvalidateForRead();
			return m_Memory->GetMapped();
		}

	private:
		core::SharedRef<BufferMemory> m_Memory;
	};
}
