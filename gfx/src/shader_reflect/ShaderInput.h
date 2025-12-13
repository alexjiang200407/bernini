#pragma once

namespace gfx
{
	class ShaderInput
	{
	public:
		enum class Attribute
		{
			kInvalid = -1,
			kPosition,
			kUV,
			kNormal,
			kTangent,
			kTotal,
		};

		struct VertexInput
		{
			Attribute     attribute     = Attribute::kInvalid;
			uint32_t      semanticIndex = 0u;
			nvrhi::Format format        = nvrhi::Format::UNKNOWN;
		};

		using iterator       = std::vector<VertexInput>::iterator;
		using const_iterator = std::vector<VertexInput>::const_iterator;

	public:
		ShaderInput() = default;
		ShaderInput(nvrhi::ShaderHandle vertexShader);

		iterator
		begin() noexcept
		{
			return m_vertexInputs.begin();
		}

		iterator
		end() noexcept
		{
			return m_vertexInputs.end();
		}

		const_iterator
		begin() const noexcept
		{
			return m_vertexInputs.begin();
		}

		const_iterator
		end() const noexcept
		{
			return m_vertexInputs.end();
		}

		const_iterator
		cbegin() const noexcept
		{
			return m_vertexInputs.cbegin();
		}

		const_iterator
		cend() const noexcept
		{
			return m_vertexInputs.cend();
		}

		bool
		Empty() const noexcept
		{
			return m_vertexInputs.empty();
		}

	private:
		std::vector<VertexInput> m_vertexInputs;
	};

}
