#pragma once

namespace gfx::geom
{
	class Cube
	{
	public:
		Cube(nvrhi::DeviceHandle& device);

	private:
		nvrhi::BufferHandle      indexBuffer;
		nvrhi::BufferHandle      vertexBuffer;
		nvrhi::InputLayoutHandle inputLayout;
		nvrhi::ShaderHandle      vertexShader;
		nvrhi::ShaderHandle      pixelShader;
	};
}
