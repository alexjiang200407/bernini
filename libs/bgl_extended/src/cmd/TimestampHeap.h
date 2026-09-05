#pragma once
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <span>

namespace bgl
{
	/**
	 * A fixed number of GPU timestamp slots, written by a command list's timed spans and read back
	 * once the submission that wrote them has completed.
	 *
	 * A slot holds raw ticks; ICommandQueue::GetTimestampFrequency converts them to seconds. A slot
	 * no span has written since creation, or one the GPU could not sample, reads as
	 * c_UnwrittenTimestamp. That is zero on both backends' freshly created storage, and a GPU clock
	 * reads zero only at the instant it starts, which no heap is alive to sample.
	 */
	class ITimestampHeap : public core::Ref
	{
	public:
		static constexpr uint64_t c_UnwrittenTimestamp = 0;

		ITimestampHeap() noexcept                      = default;
		ITimestampHeap(const ITimestampHeap&) noexcept = delete;
		ITimestampHeap(ITimestampHeap&&) noexcept      = delete;

		ITimestampHeap&
		operator=(const ITimestampHeap&) noexcept = delete;

		ITimestampHeap&
		operator=(ITimestampHeap&&) noexcept = delete;

		[[nodiscard]] virtual uint32_t
		GetCapacity() const noexcept = 0;

		/**
		 * Copies slots [first, first + out.size()) into `out`, in ticks.
		 *
		 * @pre every submission that wrote or resolved those slots has completed on its queue.
		 * @pre the range lies within GetCapacity().
		 */
		virtual void
		Read(uint32_t first, std::span<uint64_t> out) const noexcept = 0;
	};

	using TimestampHeapRef = core::SharedRef<ITimestampHeap>;

	/**
	 * The span between two slots in milliseconds, or zero when either slot went unwritten, the
	 * span runs backwards, or the queue could not say its tick rate.
	 */
	[[nodiscard]] constexpr double
	TimestampSpanMilliseconds(uint64_t start, uint64_t end, double ticksPerSecond) noexcept
	{
		if (start == ITimestampHeap::c_UnwrittenTimestamp ||
		    end == ITimestampHeap::c_UnwrittenTimestamp || end < start || ticksPerSecond <= 0.0)
		{
			return 0.0;
		}
		return static_cast<double>(end - start) * 1000.0 / ticksPerSecond;
	}
}
