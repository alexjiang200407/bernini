#include "shader_reflect/ShaderInput.h"

namespace
{
	nvrhi::Format
	mapFormat(const D3D11_SIGNATURE_PARAMETER_DESC& desc)
	{
		uint32_t componentCount = (desc.Mask & 1 ? 1 : 0) + (desc.Mask & 2 ? 1 : 0) +
		                          (desc.Mask & 4 ? 1 : 0) + (desc.Mask & 8 ? 1 : 0);

		if (desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
		{
			if (componentCount == 2)
				return nvrhi::Format::RG32_FLOAT;
			if (componentCount == 3)
				return nvrhi::Format::RGB32_FLOAT;
			if (componentCount == 4)
				return nvrhi::Format::RGBA32_FLOAT;
		}

		auto description = std::format(
			"Unsupported shader input format: ComponentType={}, Mask={}",
			static_cast<uint32_t>(desc.ComponentType),
			static_cast<uint32_t>(desc.Mask));

		throw core::except::BerniniException{ "Shader Reflection Error", description };
	}
}

namespace gfx
{
	ShaderVertexInput::Attribute
	mapSemantic(const char* semantic)
	{
		using Attribute = ShaderVertexInput::Attribute;

		if (strcmp(semantic, "POSITION") == 0)
			return Attribute::kPosition;
		if (strcmp(semantic, "NORMAL") == 0)
			return Attribute::kNormal;
		if (strcmp(semantic, "TEXCOORD") == 0)
			return Attribute::kUV;
		if (strcmp(semantic, "TANGENT") == 0)
			return Attribute::kTangent;

		return Attribute::kInvalid;
	}

	ShaderVertexInput::ShaderVertexInput(nvrhi::ShaderHandle vertexShader)
	{
		auto reflector = nvrhi::RefCountPtr<ID3D11ShaderReflection>{};

		auto vertexShaderBytecode = static_cast<const void*>(nullptr);
		auto size                 = size_t{};

		vertexShader->getBytecode(&vertexShaderBytecode, &size);

		D3DReflect(vertexShaderBytecode, size, IID_PPV_ARGS(&reflector));

		auto shaderDesc = D3D11_SHADER_DESC{};
		reflector->GetDesc(&shaderDesc);

		for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
		{
			auto paramDesc = D3D11_SIGNATURE_PARAMETER_DESC{};
			reflector->GetInputParameterDesc(i, &paramDesc);

			if (paramDesc.SystemValueType != D3D_NAME_UNDEFINED)
				continue;

			auto attr = mapSemantic(paramDesc.SemanticName);

			if (attr == Attribute::kInvalid)
				continue;

			auto input          = VertexInput{};
			input.attribute     = attr;
			input.semanticIndex = paramDesc.SemanticIndex;
			input.semanticName  = std::string{ paramDesc.SemanticName };
			input.semanticId = std::format("{}{}", paramDesc.SemanticName, paramDesc.SemanticIndex);
			input.format     = mapFormat(paramDesc);

			m_vertexInputs.push_back(input);
		}
	}

}
