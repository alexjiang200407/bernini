#include "fg/PassTimer.h"
#include "cmd/CommandList.h"
#include "cmd/TimestampHeap.h"
#include <bgl_common/gassert.h>
#include <cstdint>
#include <string>
#include <string_view>

namespace bgl
{
	void
	PassTimer::Arm(ITimestampHeap& heap, uint32_t firstSlot, uint32_t maxPasses) noexcept
	{
		gassert(!m_PassOpen, "PassTimer::Arm inside a pass");
		gassert(
			firstSlot + 2 * maxPasses <= heap.GetCapacity(),
			"PassTimer::Arm range outside the heap");

		m_Heap      = &heap;
		m_FirstSlot = firstSlot;
		m_NextSlot  = firstSlot;
		m_EndSlot   = firstSlot + 2 * maxPasses;
		m_Entries.clear();
	}

	void
	PassTimer::Disarm() noexcept
	{
		gassert(!m_PassOpen, "PassTimer::Disarm inside a pass");
		m_Heap = nullptr;
	}

	void
	PassTimer::BeginPass(ICommandList* cmd, std::string_view name)
	{
		if (m_Heap == nullptr)
			return;

		gassert(!m_PassOpen, "PassTimer::BeginPass while a pass is open");
		gassert(cmd != nullptr, "PassTimer::BeginPass needs a command list");

		Entry& entry = m_Entries.emplace_back();
		entry.name   = std::string(name);
		m_PassOpen   = true;
		m_PassTimed  = m_NextSlot + 2 <= m_EndSlot;

		if (!m_PassTimed)
		{
			if (!m_Overflowed)
			{
				logger::warn(
					"PassTimer: more than {} passes in a frame; '{}' and later are not timed",
					(m_EndSlot - m_FirstSlot) / 2,
					name);
				m_Overflowed = true;
			}
			return;
		}

		entry.startSlot = m_NextSlot;
		entry.endSlot   = m_NextSlot + 1;
		m_NextSlot += 2;
		cmd->BeginTiming(*m_Heap, entry.startSlot, entry.endSlot);
	}

	void
	PassTimer::EndPass(ICommandList* cmd) noexcept
	{
		if (m_Heap == nullptr)
			return;

		gassert(m_PassOpen, "PassTimer::EndPass without a pass open");
		m_PassOpen = false;

		if (m_PassTimed)
		{
			m_Entries.back().sampled = cmd->EndTiming();
		}
	}
}
