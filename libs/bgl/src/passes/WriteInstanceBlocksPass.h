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
	class WriteInstanceBlocksPass
	{
	public:
		WriteInstanceBlocksPass() noexcept = default;
		~WriteInstanceBlocksPass() noexcept { spdlog::trace("~WriteInstanceBlocksPass"); }

		WriteInstanceBlocksPass(const WriteInstanceBlocksPass&) noexcept = delete;
		WriteInstanceBlocksPass(WriteInstanceBlocksPass&&) noexcept      = delete;

		WriteInstanceBlocksPass&
		operator=(const WriteInstanceBlocksPass&) noexcept = delete;

		WriteInstanceBlocksPass&
		operator=(WriteInstanceBlocksPass&&) noexcept = delete;

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

	private:
		static void
		Execute(const PassContext& ctx, const DrawData& draw);
	};
}
