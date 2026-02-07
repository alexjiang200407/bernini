#pragma once
#include "buffer/DynamicConstantBuffer.h"

class FrameGraph;
class FrameGraphBlackboard;

namespace gfx
{
	class MeshRegistry;
	class Camera;

	class SetupFrameDataPass final
	{
	public:
		void
		Init(nvrhi::DeviceHandle device, MeshRegistry& registry);

		void
		AttachToFrameGraph(
			FrameGraph&           frameGraph,
			FrameGraphBlackboard& blackBoard,
			MeshRegistry&         registry,
			Camera&               camera,
			nvrhi::DeviceHandle   device);

		nvrhi::BindingLayoutHandle
		GetBindingLayout() noexcept
		{
			return m_blPerFrame;
		}

	private:
		nvrhi::CommandListHandle   m_mainCommandList;
		DynamicConstantBuffer      m_frameConstants;
		nvrhi::BindingSetHandle    m_bsPerFrame;
		nvrhi::BindingLayoutHandle m_blPerFrame;
	};

}
