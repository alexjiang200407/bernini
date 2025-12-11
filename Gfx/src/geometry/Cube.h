#pragma once
#include "drawable/IDrawable.h"

namespace gfx
{
	class Cube : public IDrawable
	{
	public:
		Cube(nvrhi::DeviceHandle& device);

		void
		AttachVertexLayout(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept override;

		void
		AttachVertexShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept override;

		void
		AttachPixelShader(nvrhi::GraphicsPipelineDesc& pipelineDesc) const noexcept override;

		void
		Draw(DrawParams params) override;

	public:
		nvrhi::BufferHandle      indexBuffer;
		nvrhi::BufferHandle      vertexBuffer;
		nvrhi::InputLayoutHandle vertexLayout;
		nvrhi::ShaderHandle      vertexShader;
		nvrhi::ShaderHandle      pixelShader;
	};
}
