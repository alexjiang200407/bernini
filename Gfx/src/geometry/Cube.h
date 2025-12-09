#pragma once

namespace gfx::geom
{
	class Cube
	{
	public:
		Cube(nvrhi::DeviceHandle& device);

		void
		Draw(nvrhi::CommandListHandle cmdList, nvrhi::GraphicsState& state) const;

		nvrhi::InputLayoutHandle
		GetInputLayout() const noexcept
		{
			return inputLayout;
		}

	public:
		nvrhi::BufferHandle      indexBuffer;
		nvrhi::BufferHandle      vertexBuffer;
		nvrhi::InputLayoutHandle inputLayout;
		nvrhi::ShaderHandle      vertexShader;
		nvrhi::ShaderHandle      pixelShader;
	};
}
