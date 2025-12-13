#pragma once
#include "shader_reflect/ShaderInput.h"

namespace gfx
{
	class IGeometry
	{
	public:
		IGeometry(nvrhi::DeviceHandle device, std::string_view vertexShaderPath);

		virtual ~IGeometry() = default;

		virtual nvrhi::InputLayoutHandle
		GetVertexLayout() const noexcept = 0;

		virtual nvrhi::BufferHandle
		GetVertexBuffer() const noexcept = 0;

		virtual nvrhi::BufferHandle
		GetIndexBuffer() const noexcept = 0;

		virtual uint32_t
		GetIndexCount() const noexcept = 0;

	protected:
		nvrhi::ShaderHandle m_vertexShader;
		ShaderInput         m_shaderInput;
	};
}
