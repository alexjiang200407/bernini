#pragma once
#include "passes/PassInitContext.h"
#include <bgpu/pipeline/MeshletKernel.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <core/glm.h>
#include <cstdint>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace bgl
{
	class FrameGraph;
	class PassContext;

	/**
	 * Renders the bloom ladder: a 13-tap downsample per level -- the first one prefiltering by
	 * threshold and Karis-weighting its quads -- then a 9-tap tent upsample per level folding the
	 * coarser result back in by scatter. Runs between the resolved scene and the post-process,
	 * which samples the finished level 0 and applies intensity in its combine.
	 *
	 * One graph pass per level in each direction, so the scheduler sees every dependency and the
	 * pass timer prices each level on its own.
	 */
	class BloomPass
	{
	public:
		struct LevelArgs
		{
			bgpu::SrvHandle downSrv;
			bgpu::RtvHandle downRtv;
			bgpu::SrvHandle upSrv;
			bgpu::RtvHandle upRtv;
			std::string     downName;
			std::string     upName;
			uint32_t        width  = 0;
			uint32_t        height = 0;
		};

		struct Args
		{
			// The last HDR stage's output, and the grid it is on: the resolved history when TAA
			// ran, the scene colour otherwise.
			bgpu::SrvHandle source;
			std::string     sourceName;
			glm::vec2       sourceSize{ 0.0f };

			// Top level first; at least one. Sizing is the chain's business, not this pass's.
			std::vector<LevelArgs> levels;

			bgpu::SamplerHandle sampler;

			// BloomSettings, with the knee premultiplied: threshold * softKnee, so the shader
			// never divides by a user value.
			float threshold = 0.0f;
			float knee      = 0.0f;
			float scatter   = 0.0f;
		};

		BloomPass() = default;
		~BloomPass() noexcept { spdlog::trace("~BloomPass"); }

		BloomPass(const BloomPass&) noexcept = delete;
		BloomPass(BloomPass&&) noexcept      = delete;

		BloomPass&
		operator=(const BloomPass&) noexcept = delete;

		BloomPass&
		operator=(BloomPass&&) noexcept = delete;

		void
		Release()
		{
			m_DownsampleKernel.Reset();
			m_UpsampleKernel.Reset();
		}

		void
		Init(const PassInitContext& ctx);

		/** @pre the batch Init requested into has been built. Fatal on a binder name the PSO lacks. */
		void
		CheckBindings() const;

		void
		AttachToFrameGraph(FrameGraph& fg, const Args& args);

	private:
		void
		ExecuteDownsample(const Args& args, uint32_t level, const PassContext& resources);

		void
		ExecuteUpsample(const Args& args, uint32_t level, const PassContext& resources);

		bgpu::MeshletKernel m_DownsampleKernel;
		bgpu::MeshletKernel m_UpsampleKernel;

		// The frame's args, shared by every level's exec lambda instead of copied into each.
		Args m_Args;
	};
}
