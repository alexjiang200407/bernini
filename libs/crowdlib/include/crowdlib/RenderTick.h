#pragma once
#include <bgpu/cmd/QueuePoint.h>
#include <cstdint>
#include <vector>

namespace crowd
{
	/** One agent type's records in a tick: [first, first + count) of the ring's buffer. */
	struct RenderTypeRecords
	{
		uint32_t first = 0;
		uint32_t count = 0;
	};

	/**
	 * Where one tick's agents are in a crowd's render ring: RenderAgent records
	 * [firstRecord, firstRecord + agentCount) of the ring's buffer, written by the crowd's queue as
	 * of `written`. A reader on another queue reads them only behind a GPU-side wait on that point,
	 * which a completed tick has already passed.
	 *
	 * The records are grouped by agent type, in CrowdDesc::agentTypes order, so a reader drawing one
	 * type reads one run: `types[t]`, empty for a type the tick has no agents of.
	 */
	struct RenderTick
	{
		uint64_t         tick        = 0;
		uint32_t         firstRecord = 0;
		uint32_t         agentCount  = 0;
		bgpu::QueuePoint written;

		std::vector<RenderTypeRecords> types;
	};
}
