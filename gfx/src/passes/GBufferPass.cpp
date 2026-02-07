#include "passes/GBufferPass.h"
#include "BindingSlots.h"
#include "camera/Camera.h"
#include "frame_graph/FrameGraphView.h"
#include "math/util.h"
#include "mesh/Mesh.h"
#include "mesh/MeshRegistry.h"
#include "mesh/Vertex.h"
#include "shader_util/util.h"
#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>

namespace gfx
{
	static auto renderState = nvrhi::RenderState{}
	                              .setRasterState(
									  nvrhi::RasterState{}
										  .setCullMode(nvrhi::RasterCullMode::None)
										  .setFillMode(nvrhi::RasterFillMode::Solid))
	                              .setDepthStencilState(
									  nvrhi::DepthStencilState{}
										  .setDepthTestEnable(true)
										  .setDepthWriteEnable(true)
										  .setDepthFunc(nvrhi::ComparisonFunc::Less)
										  .setStencilEnable(false));

}
