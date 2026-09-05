#pragma once
#include "metal_cpp.h"

#include "cmd/TimestampHeap.h"

#include <core/ref/RefCounter.h>
#include <cstdint>
#include <span>

namespace bgl
{
	/**
	 * The RHI timestamp heap over an MTL::CounterSampleBuffer of the device's timestamp counter set.
	 *
	 * A sample is written only at an encoder's stage boundary on Apple GPUs, so it is the command
	 * list's encoder descriptors that name the slots; this owns the buffer and resolves it on the
	 * CPU, which is a copy out of shared storage rather than a GPU command.
	 */
	class TimestampHeap final : public core::RefCounter<ITimestampHeap>
	{
	public:
		TimestampHeap(NS::SharedPtr<MTL::CounterSampleBuffer> buffer, uint32_t capacity) noexcept;

		TimestampHeap(const TimestampHeap&) noexcept = delete;
		TimestampHeap(TimestampHeap&&) noexcept      = delete;

		TimestampHeap&
		operator=(const TimestampHeap&) noexcept = delete;

		TimestampHeap&
		operator=(TimestampHeap&&) noexcept = delete;

		[[nodiscard]] uint32_t
		GetCapacity() const noexcept override
		{
			return m_Capacity;
		}

		void
		Read(uint32_t first, std::span<uint64_t> out) const noexcept override;

		[[nodiscard]] MTL::CounterSampleBuffer*
		GetMTLSampleBuffer() const noexcept
		{
			return m_Buffer.get();
		}

	private:
		NS::SharedPtr<MTL::CounterSampleBuffer> m_Buffer;
		uint32_t                                m_Capacity = 0;
	};
}
