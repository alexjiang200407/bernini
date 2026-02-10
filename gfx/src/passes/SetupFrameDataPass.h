#pragma once
#include "buffer/DynamicConstantBuffer.h"

class FrameGraph;
class FrameGraphBlackboard;

namespace gfx
{
	class SceneData;
	class Camera;

	class SetupFrameDataPass final
	{
	public:
		nvrhi::BindingLayoutHandle
		Init(nvrhi::DeviceHandle device);

		void
		AttachToFrameGraph(
			FrameGraph&           frameGraph,
			FrameGraphBlackboard& blackBoard,
			SceneData&            sceneData,
			Camera&               camera,
			nvrhi::DeviceHandle   device,
			uint64_t              frameIdx);

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
