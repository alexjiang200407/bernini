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
		std::span<const std::byte>                  shaderByteCode,
		nvrhi::RefCountPtr<ID3D11ShaderReflection>& outReflector,
		D3D11_SHADER_DESC&                          outDesc)
	{
		D3DReflect(shaderByteCode.data(), shaderByteCode.size(), IID_PPV_ARGS(&outReflector)) >>
			dx::dxErrorChecker;
		outReflector->GetDesc(&outDesc);
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
		getReflectorAndShaderDesc(
			std::span(reinterpret_cast<const std::byte*>(shaderByteCode), size),
			outReflector,
			outDesc);
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

	void
	emitType(
		gfx::DynamicConstantBufferDesc& desc,
		ID3D11ShaderReflectionType*     type,
		const D3D11_SHADER_TYPE_DESC&   typeDesc,
		std::string_view                path)
	{
		if (typeDesc.Class == D3D_SVC_STRUCT)
		{
			desc.AddStruct(path);

			for (UINT i = 0; i < typeDesc.Members; ++i)
			{
				auto* memberType = type->GetMemberTypeByIndex(i);
				auto  memberDesc = D3D11_SHADER_TYPE_DESC{};
				memberType->GetDesc(&memberDesc);

				const char* memberName = type->GetMemberTypeName(i);

				std::string childPath = std::string(path) + "." + memberName;

				emitType(desc, memberType, memberDesc, childPath);
			}
			return;
		}

		if (typeDesc.Elements > 0)
		{
			if (typeDesc.Class == D3D_SVC_STRUCT)
			{
				desc.AddStructArray(path, typeDesc.Elements);

				auto* elemType = type->GetSubType();
				auto  elemDesc = D3D11_SHADER_TYPE_DESC{};
				elemType->GetDesc(&elemDesc);

				for (UINT i = 0; i < elemDesc.Members; ++i)
				{
					auto* memberType = elemType->GetMemberTypeByIndex(i);
					auto  memberDesc = D3D11_SHADER_TYPE_DESC{};
					memberType->GetDesc(&memberDesc);

					const char* memberName = elemType->GetMemberTypeName(i);

					emitType(desc, memberType, memberDesc, std::string(path) + "." + memberName);
				}
			}
			else
			{
				desc.AddElementArray(
					path,
					shaderVarTypeToConstantBufferType(typeDesc),
					typeDesc.Elements);
			}
			return;
		}

		desc.AddElement(path, shaderVarTypeToConstantBufferType(typeDesc));
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

	gfx::DynamicConstantBufferDesc
	getDynamicConstantBufferDesc(
		std::span<const std::byte> shaderBytes,
		uint32_t                   constantBufferSlot)
	{
		auto reflector  = nvrhi::RefCountPtr<ID3D11ShaderReflection>{};
		auto shaderDesc = D3D11_SHADER_DESC{};
		getReflectorAndShaderDesc(shaderBytes, reflector, shaderDesc);

		gfx::DynamicConstantBufferDesc        desc;
		ID3D11ShaderReflectionConstantBuffer* cbReflect = nullptr;

		for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
		{
			D3D11_SHADER_INPUT_BIND_DESC bindDesc{};
			reflector->GetResourceBindingDesc(i, &bindDesc);

			if (bindDesc.Type == D3D_SIT_CBUFFER && bindDesc.BindPoint == constantBufferSlot)
			{
				cbReflect = reflector->GetConstantBufferByName(bindDesc.Name);
				break;
			}
		}

		if (!cbReflect)
		{
			return desc;
		}

		D3D11_SHADER_BUFFER_DESC cbDesc{};
		cbReflect->GetDesc(&cbDesc);

		if (cbDesc.Name)
			desc.SetName(cbDesc.Name);

		for (UINT varIdx = 0; varIdx < cbDesc.Variables; ++varIdx)
		{
			auto* var = cbReflect->GetVariableByIndex(varIdx);

			D3D11_SHADER_VARIABLE_DESC varDesc{};
			var->GetDesc(&varDesc);

			auto*                  type = var->GetType();
			D3D11_SHADER_TYPE_DESC typeDesc{};
			type->GetDesc(&typeDesc);

			emitType(desc, type, typeDesc, varDesc.Name);
		}

		return desc;
	}

}
