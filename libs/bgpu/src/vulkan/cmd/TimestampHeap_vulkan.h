#pragma once
#include "resource/ReadbackBuffer_vulkan.h"
#include "volk_vulkan.h"
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/TimestampHeap.h>
#include <core/ref/RefCounter.h>
#include <cstdint>
#include <span>

namespace bgpu
{
	/**
	 * A timestamp query pool and the readback ResolveTimestamps copies it into. A slot is reset as a
	 * span begins, so one the GPU never wrote keeps what the readback held: zero until written.
	 */
	class TimestampHeap final : public core::RefCounter<ITimestampHeap>
	{
	public:
		/** True when the device can time a span on every queue that can run one. */
		[[nodiscard]] static bool
		Supported(const GpuContext& context) noexcept;

		TimestampHeap(GpuContextRef context, uint32_t capacity);
		~TimestampHeap() noexcept override;

		TimestampHeap(const TimestampHeap&) = delete;
		TimestampHeap(TimestampHeap&&)      = delete;
		TimestampHeap&
		operator=(const TimestampHeap&) = delete;
		TimestampHeap&
		operator=(TimestampHeap&&) = delete;

		[[nodiscard]] uint32_t
		GetCapacity() const noexcept override
		{
			return m_Capacity;
		}

		void
		Read(uint32_t first, std::span<uint64_t> out) const noexcept override;

		[[nodiscard]] VkQueryPool
		GetVkQueryPool() const noexcept
		{
			return m_Pool;
		}

		[[nodiscard]] VkBuffer
		GetReadbackVkBuffer() const noexcept
		{
			return m_Readback.GetVkBuffer();
		}

	private:
		// Declared first, destroyed last: the pool is its device's.
		GpuContextRef  m_Context;
		uint32_t       m_Capacity = 0;
		VkQueryPool    m_Pool     = VK_NULL_HANDLE;
		ReadbackBuffer m_Readback;
	};
}
