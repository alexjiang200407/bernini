#pragma once
#include "pipeline/MeshletKernel.h"
#include <bgl/PsoType.h>

namespace bgl
{
	class IDevice;
	class FrameGraph;
	class PassContext;

	struct DrawData;

	class ForwardPass
	{
	public:
		ForwardPass() = default;
		~ForwardPass() noexcept { logger::trace("~ForwardPass"); }
		ForwardPass(IDevice* device) { Init(device); }

		ForwardPass(const ForwardPass&) noexcept = delete;
		ForwardPass(ForwardPass&&) noexcept      = delete;

		ForwardPass&
		operator=(const ForwardPass&) noexcept = delete;

		ForwardPass&
		operator=(ForwardPass&&) noexcept = delete;

		void
		Release()
		{
			for (MeshletKernel& kernel : m_Kernels)
			{
				kernel.Reset();
			}
		}

		void
		Init(IDevice* device);

		void
		AttachToFrameGraph(FrameGraph& fg, const DrawData& draw);

		void
		Execute(const DrawData& draw, const PassContext& resources);

	private:
		/** Binds the geometry, material, and IBL uniforms common to every forward draw. */
		void
		BindKernel(MeshletKernel& kernel, const DrawData& draw, const PassContext& resources);

		std::array<MeshletKernel, c_PsoCount> m_Kernels;
	};
}
