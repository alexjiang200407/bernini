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
}
