#pragma once
#include <spdlog/spdlog.h>

namespace bgl
{
	class FrameGraph;
	class PassContext;
	struct DrawData;

	/**
	 * Runs every instance block's writer over its slots: one dispatch per block that has one, which
	 * writes the block's run of the view's MeshInstance buffer through its writable view. Ordered
	 * ahead of everything that reads a placement -- the pose pass and the cull -- by the read-write
	 * it declares on that buffer.
	 *
	 * Absent from the graph on a frame whose view has no written block, so a scene without one pays
	 * nothing. It compiles nothing of its own: each block brings its writer's kernel.
	 */
	class PlaceBlocksPass
	{
	public:
		PlaceBlocksPass() noexcept = default;
		~PlaceBlocksPass() noexcept { spdlog::trace("~PlaceBlocksPass"); }

		PlaceBlocksPass(const PlaceBlocksPass&) noexcept = delete;
		PlaceBlocksPass(PlaceBlocksPass&&) noexcept      = delete;

		PlaceBlocksPass&
		operator=(const PlaceBlocksPass&) noexcept = delete;

		PlaceBlocksPass&
		operator=(PlaceBlocksPass&&) noexcept = delete;

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

	private:
		static void
		Execute(const PassContext& ctx, const DrawData& draw);
	};
}
