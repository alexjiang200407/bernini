#pragma once
#include "buffer/DynamicVertexBuffer.h"
#include "drawable/IDrawable.h"
#include "geometry/CubeGeometry.h"

namespace gfx
{
	class Cube : public IDrawable
	{
	public:
		Cube(nvrhi::DeviceHandle device, std::string_view vertexShader);

		void
		AttachVertexLayout(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept override;

		void
		AttachVertexShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept override;

		void
		AttachPixelShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept override;

		void
		Draw(DrawParams params) override;

	public:
		CubeGeometry        m_geom;
		nvrhi::ShaderHandle m_pixelShader;
	};
}
