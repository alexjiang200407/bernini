#pragma once
#include "cmd/CommandList.h"
#include "cmd/TimestampHeap.h"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bgl
{
	/**
	 * Times every pass FrameGraph::Execute records, as a pair of ITimestampHeap slots per pass.
	 *
	 * Armed once per frame with the slot range that frame owns; the graph brackets each kept pass
	 * with BeginPass/EndPass, and what is left afterwards is the passes in execution order with
	 * the slots that hold their samples. Reading those slots is the caller's, once the frame's
	 * fence has passed -- the timer never touches the heap.
	 */
	class PassTimer
	{
	public:
		struct Entry
		{
			std::string name;
			uint32_t    startSlot = 0;
			uint32_t    endSlot   = 0;

			// False for a pass the GPU could not attach a sample to (ICommandList::EndTiming), and
			// for one past the armed capacity; its slots are then never read.
			bool sampled = false;
		};

		/**
		 * Arms the timer for one frame over slots [firstSlot, firstSlot + 2 * maxPasses) of `heap`.
		 * A pass beyond `maxPasses` is listed unsampled, and the overflow logged once.
		 *
		 * @pre `heap` outlives the frame -- the timer keeps its address; the range lies within its
		 *      capacity.
		 */
		void
		Arm(ITimestampHeap& heap, uint32_t firstSlot, uint32_t maxPasses) noexcept;

		void
		Disarm() noexcept;

		[[nodiscard]] bool
		IsArmed() const noexcept
		{
			return m_Heap != nullptr;
		}

		// Called by FrameGraph::Execute around each kept pass. No-ops when not armed.
		void
		BeginPass(ICommandList* cmd, std::string_view name);

		void
		EndPass(ICommandList* cmd) noexcept;

		[[nodiscard]] std::span<const Entry>
		Entries() const noexcept
		{
			return m_Entries;
		}

		[[nodiscard]] ITimestampHeap*
		GetHeap() const noexcept
		{
			return m_Heap;
		}

		[[nodiscard]] uint32_t
		GetFirstSlot() const noexcept
		{
			return m_FirstSlot;
		}

		/** How many slots the passes so far have taken, from GetFirstSlot. */
		[[nodiscard]] uint32_t
		GetSlotsUsed() const noexcept
		{
			return m_NextSlot - m_FirstSlot;
		}

	private:
		ITimestampHeap*    m_Heap       = nullptr;
		uint32_t           m_FirstSlot  = 0;
		uint32_t           m_EndSlot    = 0;
		uint32_t           m_NextSlot   = 0;
		bool               m_Overflowed = false;
		bool               m_PassOpen   = false;
		bool               m_PassTimed  = false;
		std::vector<Entry> m_Entries;
	};
}
