#include "geometry/Cube.h"
#include <core/file/file.h>

namespace gfx
{
	Cube::Cube(nvrhi::DeviceHandle device, std::string_view vertexShader) :
		IDrawable{ {} }, m_geom{ device, vertexShader }
	{
		auto pixelShaderData = core::file::readFileBytes("shaders/PS_cube.cso"sv);
		m_pixelShader        = device->createShader(
            nvrhi::ShaderDesc()
                .setShaderType(nvrhi::ShaderType::Pixel)
                .setDebugName("Cube Pixel Shader"),
            pixelShaderData.data(),
            pixelShaderData.size());
	}

	void
	Cube::AttachVertexLayout(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept
	{
		pipelineDesc.setInputLayout(m_geom.GetVertexLayout());
	}

	void
	Cube::AttachVertexShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept
	{
		pipelineDesc.setVertexShader(m_geom.GetVertexShader());
	}

	void
	Cube::AttachPixelShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept
	{
		pipelineDesc.setPixelShader(m_pixelShader);
	}

	void
	Cube::Draw(DrawParams params)
	{
		auto& state   = params.gfxState;
		auto  cmdList = params.commandList;

		m_geom.AttachGeometry(state);

		cmdList->setGraphicsState(state);

		cmdList->drawIndexed(
			nvrhi::DrawArguments{}
				.setVertexCount(m_geom.GetIndexCount())
				.setStartIndexLocation(0)
				.setInstanceCount(1));
	}
}
