#pragma once
#include "resource/BufferMemory_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/MemoryTag.h>
#include <bgpu/resource/Buffer.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <utility>

namespace bgpu
{
	/** A buffer in a manager's pool: its memory, the descriptor it is reached by, and its desc. */
	class Buffer final
	{
	public:
		Buffer() = default;

		/**
		 * @param tracked whether the bytes are charged here: a buffer adopted from another owner
		 * stays charged to the owner that allocated it.
		 */
		Buffer(
			core::SharedRef<BufferMemory> memory,
			uint32_t                      descriptorIndex,
			BufferDesc                    desc,
			bool                          tracked) :
			m_Desc(std::move(desc)), m_DescriptorIndex(descriptorIndex), m_Memory(std::move(memory))
		{
			if (tracked)
				m_Tracked = TaggedBytes(MemoryTag::kDeviceBuffer, m_Desc.byteSize);
		}

		~Buffer() noexcept = default;

		Buffer(const Buffer&)     = delete;
		Buffer(Buffer&&) noexcept = default;
		Buffer&
		operator=(const Buffer&) = delete;
		Buffer&
		operator=(Buffer&&) noexcept = default;

		[[nodiscard]] VkBuffer
		GetVkBuffer() const noexcept
		{
			return m_Memory != nullptr ? m_Memory->GetVkBuffer() : VK_NULL_HANDLE;
		}

		[[nodiscard]] const BufferDesc&
		GetDesc() const noexcept
		{
			return m_Desc;
		}

		[[nodiscard]] uint32_t
		GetDescriptorIndex() const noexcept
		{
			return m_DescriptorIndex;
		}

		[[nodiscard]] bool
		IsNull() const noexcept
		{
			return m_Memory == nullptr;
		}

	private:
		BufferDesc                    m_Desc;
		uint32_t                      m_DescriptorIndex = 0xFFFFFFFF;
		core::SharedRef<BufferMemory> m_Memory;

		// The size asked for, not the driver's: alignment padding differs per backend.
		TaggedBytes m_Tracked;
	};
}
