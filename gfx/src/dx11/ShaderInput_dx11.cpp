#include "shader_reflect/ShaderInput.h"

namespace
{
	using namespace gfx;

	ElementType
	shaderVarTypeToConstantBufferType(const D3D11_SHADER_TYPE_DESC& typeDesc)
	{
		switch (typeDesc.Type)
		{
		case D3D_SVT_FLOAT:
			{
				if (typeDesc.Columns == 4 && typeDesc.Rows == 4)
					return ElementType::kFloat4x4;

				if (typeDesc.Rows > 1)
				{
					throw GfxException{ GFX_RESULT_ERROR_SHADER_REFLECT,
						                "Shader Reflection Error",
						                "Unsupported matrix type in constant buffer." };
				}

				if (typeDesc.Columns == 4)
					return ElementType::kFloat4;

				if (typeDesc.Columns == 3)
					return ElementType::kFloat3;

				if (typeDesc.Columns == 2)
					return ElementType::kFloat2;

				return ElementType::kFloat;
			}
		default:
			throw GfxException{ GFX_RESULT_ERROR_SHADER_REFLECT,
				                "Shader Reflection Error",
				                std::format(
									"Unsupported shader variable type: {}",
									static_cast<uint32_t>(typeDesc.Type)) };
		}
	}

	void
	getReflectorAndShaderDesc(
		nvrhi::ShaderHandle                         shader,
		nvrhi::RefCountPtr<ID3D11ShaderReflection>& outReflector,
		D3D11_SHADER_DESC&                          outDesc)
	{
		auto shaderByteCode = static_cast<const void*>(nullptr);
		auto size           = size_t{};

		shader->getBytecode(&shaderByteCode, &size);
		D3DReflect(shaderByteCode, size, IID_PPV_ARGS(&outReflector)) >> dx::dxErrorChecker;
		outReflector->GetDesc(&outDesc);
	}

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

		throw GfxException{ GFX_RESULT_ERROR_SHADER_REFLECT,
			                "Shader Reflection Error",
			                description };
	}
}

namespace gfx
{
	VertexAttribute::Type
	mapSemantic(const char* semantic)
	{
		using Type = VertexAttribute::Type;

		if (strcmp(semantic, "POSITION") == 0)
			return Type::kPosition;
		if (strcmp(semantic, "NORMAL") == 0)
			return Type::kNormal;
		if (strcmp(semantic, "TEXCOORD") == 0)
			return Type::kUV;
		if (strcmp(semantic, "TANGENT") == 0)
			return Type::kTangent;

		return Type::kInvalid;
	}

	std::vector<VertexAttribute>
	getVertexAttributes(nvrhi::ShaderHandle shader)
	{
		auto reflector  = nvrhi::RefCountPtr<ID3D11ShaderReflection>{};
		auto shaderDesc = D3D11_SHADER_DESC{};
		getReflectorAndShaderDesc(shader, reflector, shaderDesc);

		UINT shaderType = D3D11_SHVER_GET_TYPE(shaderDesc.Version);
		if (shaderType != D3D11_SHVER_VERTEX_SHADER)
		{
			throw GfxException{ GFX_RESULT_ERROR_SHADER_REFLECT,
				                "Shader Reflection Error",
				                "Provided shader is not a vertex shader." };
		}

		auto vertexInputs = std::vector<gfx::VertexAttribute>{};

		for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
		{
			auto paramDesc = D3D11_SIGNATURE_PARAMETER_DESC{};
			reflector->GetInputParameterDesc(i, &paramDesc);

			if (paramDesc.SystemValueType != D3D_NAME_UNDEFINED)
				continue;

			auto attr = mapSemantic(paramDesc.SemanticName);

			if (attr == VertexAttribute::kInvalid)
				continue;

			auto input          = gfx::VertexAttribute{};
			input.type          = attr;
			input.semanticIndex = paramDesc.SemanticIndex;
			input.semanticName  = std::string{ paramDesc.SemanticName };
			input.semanticId = std::format("{}{}", paramDesc.SemanticName, paramDesc.SemanticIndex);
			input.format     = mapFormat(paramDesc);

			vertexInputs.push_back(input);
		}

		return vertexInputs;
	}

	std::vector<ConstantBufferInput>
	getConstantBufferInputs(nvrhi::ShaderHandle shader)
	{
		auto reflector  = nvrhi::RefCountPtr<ID3D11ShaderReflection>{};
		auto shaderDesc = D3D11_SHADER_DESC{};
		getReflectorAndShaderDesc(shader, reflector, shaderDesc);

		auto cbufCount = shaderDesc.ConstantBuffers;
		auto cbufs     = std::vector<gfx::ConstantBufferInput>{};

		for (UINT i = 0; i < cbufCount; ++i)
		{
			auto cbDesc = D3D11_SHADER_BUFFER_DESC{};
			reflector->GetConstantBufferByIndex(i)->GetDesc(&cbDesc);

			for (UINT cbIdx = 0; cbIdx < cbDesc.Variables; ++cbIdx)
			{
				auto*               cbReflect = reflector->GetConstantBufferByIndex(cbIdx);
				ConstantBufferInput cb;
				cb.name = cbDesc.Name;
				cb.size = cbDesc.Size;
				cb.slot = i;
				cb.space;

				auto cbDesc = D3D11_SHADER_BUFFER_DESC{};
				cbReflect->GetDesc(&cbDesc);

				for (UINT varIdx = 0; varIdx < cbDesc.Variables; ++varIdx)
				{
					auto* var         = cbReflect->GetVariableByIndex(varIdx);
					auto  varDesc     = D3D11_SHADER_VARIABLE_DESC{};
					auto* varType     = var->GetType();
					auto  varTypeDesc = D3D11_SHADER_TYPE_DESC{};

					varType->GetDesc(&varTypeDesc);
					var->GetDesc(&varDesc);

					cb.entries.emplace_back(
						varDesc.Name,
						varDesc.StartOffset,
						shaderVarTypeToConstantBufferType(varTypeDesc));
				}

				cbufs.push_back(std::move(cb));
			}
		}

		return cbufs;
	}

}
