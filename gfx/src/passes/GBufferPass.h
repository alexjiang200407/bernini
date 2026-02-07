#pragma once
#include "buffer/ComputeBuffer.h"
#include "buffer/DynamicConstantBuffer.h"

class FrameGraph;
class FrameGraphBlackboard;

namespace gfx
{
	struct FrameData;

	struct RenderArgs
	{
		float                    screenWidth;
		float                    screenHeight;
		nvrhi::DeviceHandle      device;
		nvrhi::FramebufferHandle outBuffer;
		nvrhi::FramebufferInfo   outBufferInfo;
	};

	class GBufferPass
	{};
}
