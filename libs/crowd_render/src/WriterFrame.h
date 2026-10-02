#pragma once
#include "idl/WriterFrame.h"
#include <bgl/glm.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/uniforms/UniformsBase.h>
#include <crowdlib/ICrowd.h>
#include <cstdint>

namespace crowd_render
{
	/**
	 * What one frame's writer reads besides its type: the three ticks it may touch, and how far
	 * between the last two this frame and last frame stood.
	 */
	struct WriterFrame
	{
		idl::TickRecords current;
		idl::TickRecords previous;
		idl::TickRecords older;
		float            alpha     = 0.0f;
		float            lastAlpha = 0.0f;
		uint32_t         lastFrame = idl::c_LastFrameNone;
	};

	/**
	 * The frame drawing `tick`, `alpha` of the way from the tick before, after a frame that drew
	 * `lastTick` at `lastAlpha` -- 0 when there was none. A tick the ring no longer holds, or that
	 * never was, has no records.
	 */
	[[nodiscard]] WriterFrame
	PlanWriterFrame(
		const crowd::ICrowd& crowd,
		uint64_t             tick,
		float                alpha,
		uint64_t             lastTick,
		float                lastAlpha);

	/** Writes CrowdInstanceWriter's Params for agent type `type`, posed by `model`. */
	void
	WriteWriterParams(
		bgpu::UniformsBase::Accessor params,
		const WriterFrame&           frame,
		bgpu::BufferHandle           ring,
		uint32_t                     type,
		const glm::mat4&             model);
}
