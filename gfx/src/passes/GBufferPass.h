#include "drawable/IDrawable.h"
#include "passes/GeomPass.h"

class FrameGraph;
class FrameGraphBlackboard;

namespace gfx
{
	struct RenderArgs
	{
		float                    screenWidth;
		float                    screenHeight;
		nvrhi::DeviceHandle      device;
		nvrhi::FramebufferHandle outBuffer;
		nvrhi::FramebufferInfo   outBufferInfo;
	};

	class GBufferPass : public GeomPass
	{
	public:
		void
		AttachToFrameGraph(
			FrameGraph&                           frameGraph,
			FrameGraphBlackboard&                 blackBoard,
			RenderArgs                            renderArgs,
			Camera&                               camera,
			std::span<std::unique_ptr<IDrawable>> draw);
	};
}
