#include "geometry/IGeometry.h"
#include <core/file/file.h>

namespace gfx
{
	IGeometry::IGeometry(nvrhi::DeviceHandle device, std::string_view vertexShaderPath)
	{
		auto vertexShaderData = core::file::readFileBytes(vertexShaderPath);
		m_vertexShader        = device->createShader(
            nvrhi::ShaderDesc{}
                .setShaderType(nvrhi::ShaderType::Vertex)
                .setDebugName("Vertex Shader"),
            vertexShaderData.data(),
            vertexShaderData.size());
		m_shaderInput = ShaderVertexInput{ m_vertexShader };
	}

	void
	IGeometry::AttachGeometry(nvrhi::GraphicsState& state) const noexcept
	{
		nvrhi::VertexBufferBinding vbufBinding;
		vbufBinding.buffer = GetVertexBuffer();
		vbufBinding.slot   = 0;
		vbufBinding.offset = 0;

		nvrhi::IndexBufferBinding ibufBinding;
		ibufBinding.buffer = GetIndexBuffer();
		ibufBinding.format = nvrhi::Format::R16_UINT;
		ibufBinding.offset = 0;

		state.addVertexBuffer(vbufBinding);
		state.setIndexBuffer(ibufBinding);
	}

	nvrhi::ShaderHandle
	IGeometry::GetVertexShader() const noexcept
	{
		return m_vertexShader;
	}
}
