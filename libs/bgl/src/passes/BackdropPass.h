#pragma once
#include "passes/PassInitContext.h"
#include <bgpu/pipeline/MeshletKernel.h>
#include <spdlog/spdlog.h>

namespace bgl
{
	class FrameGraph;
	class PassContext;
	struct DrawData;

	/** Draws the view's backdrop gradient behind the scene, where the skybox would otherwise be. */
	class BackdropPass
	{
	public:
		explicit BackdropPass(const PassInitContext& ctx);
		~BackdropPass() noexcept { spdlog::trace("~BackdropPass"); }

		BackdropPass(const BackdropPass&) noexcept = delete;
		BackdropPass(BackdropPass&&) noexcept      = delete;

		BackdropPass&
		operator=(const BackdropPass&) noexcept = delete;

		BackdropPass&
		operator=(BackdropPass&&) noexcept = delete;

		/** @pre the constructor's batch has been built. Fatal on a binder name the PSO lacks. */
		void
		CheckBindings() const;

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

		void
		Execute(const DrawData& draw, const PassContext& resources);

	private:
		bgpu::MeshletKernel m_Kernel;
	};
}
