#pragma once
#include "math/util.h"

namespace gfx
{
	/// <summary>
	/// Manages conversion of glm::mat4 to shader-compatible layout based on the graphics API.
	/// </summary>
	class ShaderMatrix
	{
	public:
		ShaderMatrix() noexcept : m_data(1.0f) {}
		ShaderMatrix(const ShaderMatrix&) noexcept = default;
		ShaderMatrix(ShaderMatrix&&) noexcept      = default;

		explicit ShaderMatrix(const glm::mat4& mat) noexcept;
		explicit ShaderMatrix(glm::mat4&& mat) noexcept;

		ShaderMatrix&
		operator=(const ShaderMatrix&) noexcept = default;

		ShaderMatrix&
		operator=(ShaderMatrix&&) noexcept = default;

		ShaderMatrix&
		operator=(const glm::mat4& mat) noexcept;

		ShaderMatrix&
		operator=(glm::mat4&& mat) noexcept;

		operator const glm::mat4&() const noexcept;
		operator glm::mat4() const noexcept;

	private:
		glm::mat4 m_data;
	};

	static_assert(sizeof(ShaderMatrix) == 64, "ShaderMatrix size must be 64 bytes");
}
