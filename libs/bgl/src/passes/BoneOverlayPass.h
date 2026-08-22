#pragma once
#include "pipeline/MeshletKernel.h"

namespace bgl
{
	class FrameGraph;
	class IDevice;
	class PassContext;
	struct DrawData;

	/**
	 * Draws every skinned instance's skeleton as octahedral solids, into a colour target of the
	 * overlay's own that the post-process composites over the tonemapped frame.
	 *
	 * The pose comes out of the palette the pose pass wrote, so the skeleton on screen is the one
	 * that skins the mesh beside it. Ordered after that pass and before the composite; attached only
	 * when the draw's target has the overlay on and the view places something skinned.
	 *
	 * Its depth attachment is the overlay's own, never the scene's: the bones occlude each other and
	 * are never occluded by the skin they are inside.
	 */
	class BoneOverlayPass
	{
	public:
		BoneOverlayPass() = default;
		~BoneOverlayPass() noexcept { logger::trace("~BoneOverlayPass"); }

		BoneOverlayPass(const BoneOverlayPass&) noexcept = delete;
		BoneOverlayPass(BoneOverlayPass&&) noexcept      = delete;

		BoneOverlayPass&
		operator=(const BoneOverlayPass&) noexcept = delete;

		BoneOverlayPass&
		operator=(BoneOverlayPass&&) noexcept = delete;

		void
		Init(IDevice* device);

		void
		Release();

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

	private:
		void
		Execute(const DrawData& draw, const PassContext& ctx);

		MeshletKernel m_Kernel;
	};
}
