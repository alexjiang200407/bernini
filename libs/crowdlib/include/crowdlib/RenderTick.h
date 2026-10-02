#pragma once
#include <bgpu/cmd/QueuePoint.h>
#include <cstdint>

namespace crowd
{
	/**
	 * Where one tick's agents are in a crowd's render ring: RenderAgent records
	 * [firstRecord, firstRecord + agentCount) of the ring's buffer, written by the crowd's queue as
	 * of `written`. A reader on another queue reads them only behind a GPU-side wait on that point,
	 * which a completed tick has already passed.
	 */
	struct RenderTick
	{
		uint64_t         tick        = 0;
		uint32_t         firstRecord = 0;
		uint32_t         agentCount  = 0;
		bgpu::QueuePoint written;
	};
}
