#include "WriterFrame.h"
#include "idl/WriterFrame.h"
#include <bgl/glm.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/uniforms/Uniforms.h>  // IWYU pragma: keep -- assigns a BufferHandle
#include <bgpu/uniforms/UniformsBase.h>
#include <crowdlib/ICrowd.h>
#include <crowdlib/RenderTick.h>
#include <cstdint>
#include <optional>

namespace crowd_render
{
	namespace
	{
		idl::TickRecords
		RecordsOf(const crowd::ICrowd& crowd, uint64_t tick)
		{
			if (tick == 0)
				return {};
			const std::optional<crowd::RenderTick> held = crowd.GetRenderTick(tick);
			if (!held)
				return {};
			return { .firstRecordIndex = held->firstRecordIndex, .count = held->agentCount };
		}

		void
		WriteRecords(bgpu::UniformsBase::Accessor records, const idl::TickRecords& tick)
		{
			records["firstRecordIndex"] = tick.firstRecordIndex;
			records["count"]            = tick.count;
		}
	}

	WriterFrame
	PlanWriterFrame(
		const crowd::ICrowd& crowd,
		uint64_t             tick,
		float                alpha,
		uint64_t             lastTick,
		float                lastAlpha)
	{
		auto frame     = WriterFrame();
		frame.current  = RecordsOf(crowd, tick);
		frame.previous = tick > 1 ? RecordsOf(crowd, tick - 1) : idl::TickRecords();
		frame.older    = tick > 2 ? RecordsOf(crowd, tick - 2) : idl::TickRecords();
		frame.alpha    = alpha;
		if (const std::optional<crowd::RenderTick> held =
		        tick != 0 ? crowd.GetRenderTick(tick) : std::nullopt)
		{
			for (const auto& run : held->types)
				frame.types.push_back(
					{ .firstRecordIndex = run.firstRecordIndex - held->firstRecordIndex,
				      .count            = run.count });
		}
		if (lastTick != 0 && lastTick == tick)
		{
			frame.lastFrame = idl::c_LastFrameSameTick;
			frame.lastAlpha = lastAlpha;
		}
		else if (lastTick != 0 && lastTick + 1 == tick)
		{
			frame.lastFrame = idl::c_LastFrameTickBefore;
			frame.lastAlpha = lastAlpha;
		}
		return frame;
	}

	void
	WriteWriterParams(
		bgpu::UniformsBase::Accessor params,
		const WriterFrame&           frame,
		bgpu::BufferHandle           ring,
		uint32_t                     type,
		const glm::mat4&             model,
		float                        phaseSpreadSeconds)
	{
		params["ring"] = ring;
		WriteRecords(params["current"], frame.current);
		WriteRecords(params["previous"], frame.previous);
		WriteRecords(params["older"], frame.older);
		params["alpha"]     = frame.alpha;
		params["lastAlpha"] = frame.lastAlpha;
		params["lastFrame"] = frame.lastFrame;
		const auto run      = type < frame.types.size() ? frame.types[type] : idl::TickRecords();
		params["run"]["firstRecordIndex"] = run.firstRecordIndex;
		params["run"]["count"]            = run.count;
		params["model"]                   = model;
		params["phaseSpreadSeconds"]      = phaseSpreadSeconds;
	}
}
