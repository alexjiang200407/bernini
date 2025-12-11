#include "math/ShaderMatrix.h"

namespace
{
	glm::mat4
	toShaderLayout(const glm::mat4& matrix) noexcept
	{
#ifdef RENDERER_DX11
		// Right-handed to left-handed conversion for hlsl
		glm::mat4 scaleZ = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, -1.0f));
		return matrix * scaleZ;
#else
		return matrix;
#endif
	}
}

namespace gfx
{
	ShaderMatrix::ShaderMatrix(const glm::mat4& mat) noexcept : m_data(toShaderLayout(mat)) {}
	ShaderMatrix::ShaderMatrix(glm::mat4&& mat) noexcept : m_data(toShaderLayout(mat)) {}

	ShaderMatrix&
	ShaderMatrix::operator=(const glm::mat4& mat) noexcept
	{
		m_data = toShaderLayout(mat);
		return *this;
	}

	ShaderMatrix&
	ShaderMatrix::operator=(glm::mat4&& mat) noexcept
	{
		m_data = toShaderLayout(mat);
		return *this;
	}

	ShaderMatrix::operator const glm::mat4&() const noexcept { return m_data; }
	ShaderMatrix::operator glm::mat4() const noexcept { return m_data; }
}
