#include "pipeline/MeshletPipeline_wgpu.h"

#include "resource/Shader.h"

namespace bgl
{
	namespace
	{
		// The BGL_WGSL arm of the mesh module names its vertex-pulling stage VSMain; the pixel entry
		// keeps the descriptor's own name. The two may live in different modules -- the link handles
		// that -- so the forward shaders' separate pixel module works.
		GraphicsPipelineDesc
		ToGraphicsPipelineDesc(const MeshletPipelineDesc& desc)
		{
			gassert(desc.meshShader != nullptr, "MeshletPipeline: null mesh shader");
			gassert(desc.pixelShader != nullptr, "MeshletPipeline: null pixel shader");

			auto graphics         = GraphicsPipelineDesc{};
			graphics.vertexShader = desc.meshShader;
			graphics.pixelShader  = desc.pixelShader;
			graphics.vertexEntry  = "VSMain";
			graphics.pixelEntry   = desc.pixelShader->GetDesc().entryPointName.empty() ?
			                            "PSMain" :
			                            desc.pixelShader->GetDesc().entryPointName;
			graphics.renderState  = desc.renderState;
			graphics.dsvFormat    = desc.dsvFormat;

			for (const Format& format : desc.rtvFormats) graphics.rtvFormats.push_back(format);

			return graphics;
		}
	}

	MeshletPipeline::MeshletPipeline(
		const wgpu::Device&        device,
		slang::ISession*           session,
		const MeshletPipelineDesc& desc) :
		m_Desc(desc), m_Graphics(device, session, ToGraphicsPipelineDesc(desc))
	{}
}
