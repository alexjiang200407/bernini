#pragma once
#include "types/QueueType.h"

namespace bgl
{
	constexpr uint64_t c_VersionSubmittedFlag = 0x8000000000000000;
	constexpr uint32_t c_VersionQueueShift    = 60;
	constexpr uint32_t c_VersionQueueMask     = 0x7;
	constexpr uint64_t c_VersionIDMask        = 0x0FFFFFFFFFFFFFFF;

	constexpr uint64_t
	MakeVersion(uint64_t id, QueueType queue, bool submitted)
	{
		uint64_t result = (id & c_VersionIDMask) | (uint64_t(queue) << c_VersionQueueShift);
		if (submitted)
			result |= c_VersionSubmittedFlag;
		return result;
	}

	constexpr uint64_t
	VersionGetInstance(uint64_t version)
	{
		return version & c_VersionIDMask;
	}

	constexpr QueueType
	VersionGetQueue(uint64_t version)
	{
		return QueueType((version >> c_VersionQueueShift) & c_VersionQueueMask);
	}

	constexpr bool
	VersionGetSubmitted(uint64_t version)
	{
		return (version & c_VersionSubmittedFlag) != 0;
	}
}
