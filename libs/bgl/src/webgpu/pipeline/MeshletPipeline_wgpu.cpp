#include "pipeline/MeshletPipeline_wgpu.h"

#include "resource/Shader_wgpu.h"

namespace bgl
{
	namespace
	{
		// The BGL_WGSL arm of the mesh module names its vertex-pulling stage VSMain, so the vertex
		// stage is a second shader over the mesh shader's module rather than the mesh shader itself.
		// The pixel stage keeps its own, which may be a different module -- the link handles that, so
		// the forward shaders' separate pixel module works.
		GraphicsPipelineDesc
		ToGraphicsPipelineDesc(const MeshletPipelineDesc& desc, slang::ISession* session)
		{
			gassert(desc.meshShader != nullptr, "MeshletPipeline: null mesh shader");
			gassert(desc.pixelShader != nullptr, "MeshletPipeline: null pixel shader");

			auto vertexDesc            = ShaderDesc{};
			vertexDesc.slangModuleName = desc.meshShader->GetDesc().slangModuleName;
			vertexDesc.entryPointName  = "VSMain";
			vertexDesc.debugName       = desc.meshShader->GetDesc().debugName;

			auto graphics         = GraphicsPipelineDesc{};
			graphics.vertexShader = core::SharedRef<Shader>::Make(std::move(vertexDesc), session);
			graphics.pixelShader  = desc.pixelShader;
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
		m_Desc(desc), m_Graphics(
						  core::SharedRef<GraphicsPipeline>::Make(
							  device,
							  session,
							  ToGraphicsPipelineDesc(desc, session)))
	{}
}
