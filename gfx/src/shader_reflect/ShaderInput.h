#pragma once

namespace gfx
{
	bool
	semanticMatches(
		std::string_view elementName,
		std::string_view semanticName,
		uint32_t         semanticIndex);

	class ShaderVertexInput
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
			std::string   semanticName  = "";
			std::string   semanticId    = "";
			nvrhi::Format format        = nvrhi::Format::UNKNOWN;
		};

		using iterator       = std::vector<VertexInput>::iterator;
		using const_iterator = std::vector<VertexInput>::const_iterator;

	public:
		ShaderVertexInput() = default;
		ShaderVertexInput(nvrhi::ShaderHandle vertexShader);

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

		size_t
		Size() const noexcept
		{
			return m_vertexInputs.size();
		}

		const VertexInput&
		operator[](size_t index) const noexcept
		{
			return m_vertexInputs[index];
		}

	private:
		std::vector<VertexInput> m_vertexInputs;
	};

	ShaderVertexInput::Attribute
	mapSemantic(const char* semantic);
}
