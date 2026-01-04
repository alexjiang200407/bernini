class FrameGraph;
class FrameGraphBlackboard;

namespace gfx
{
	struct FrameData;
	class MeshRegistry;

	struct RenderArgs
	{
		float                    screenWidth;
		float                    screenHeight;
		nvrhi::DeviceHandle      device;
		nvrhi::FramebufferHandle outBuffer;
		nvrhi::FramebufferInfo   outBufferInfo;
	};

	class GBufferPass
	{
	public:
		void
		Init(nvrhi::DeviceHandle device);

		void
		AttachToFrameGraph(
			FrameGraph&           frameGraph,
			FrameGraphBlackboard& blackBoard,
			RenderArgs            renderArgs);

	private:
		nvrhi::CommandListHandle      m_mainCommandList;
		nvrhi::GraphicsPipelineHandle m_graphicsPipeline;
		nvrhi::ShaderHandle           m_vertexShader;
		nvrhi::ShaderHandle           m_pixelShader;
	};
}
