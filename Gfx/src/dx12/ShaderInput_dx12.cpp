#pragma once
#include "shader_reflect/ShaderInput.h"
#include <d3d12shader.h>
#include <dxcapi.h>
#include <format>
#include <stdexcept>

namespace
{
	using namespace gfx;

	ElementType
	shaderVarTypeToConstantBufferType(const D3D12_SHADER_TYPE_DESC& typeDesc)
	{
		switch (typeDesc.Type)
		{
		case D3D_SVT_FLOAT:
			if (typeDesc.Columns == 4 && typeDesc.Rows == 4)
				return ElementType::kFloat4x4;
			if (typeDesc.Rows > 1)
				throw std::runtime_error("Unsupported matrix type in constant buffer.");
			if (typeDesc.Columns == 4)
				return ElementType::kFloat4;
			if (typeDesc.Columns == 3)
				return ElementType::kFloat3;
			if (typeDesc.Columns == 2)
				return ElementType::kFloat2;
			return ElementType::kFloat;
		default:
			throw std::runtime_error(
				std::format(
					"Unsupported shader variable type: {}",
					static_cast<uint32_t>(typeDesc.Type)));
		}
	}

	void
	getReflectorAndShaderDescDXIL(
		std::span<const std::byte>                  shaderByteCode,
		nvrhi::RefCountPtr<ID3D12ShaderReflection>& outReflector,
		D3D12_SHADER_DESC&                          outDesc)
	{
		auto container = nvrhi::RefCountPtr<IDxcContainerReflection>{};

		DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(&container)) >>
			dx::dxErrorChecker;

		nvrhi::RefCountPtr<IDxcBlobEncoding> blob;
		nvrhi::RefCountPtr<IDxcLibrary>      library;
		DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library)) >> dx::dxErrorChecker;

		library->CreateBlobWithEncodingFromPinned(
			shaderByteCode.data(),
			static_cast<UINT32>(shaderByteCode.size()),
			0,  // CP_ACP, code page (ignored for binary)
			&blob) >>
			dx::dxErrorChecker;

		container->Load(blob) >> dx::dxErrorChecker;

		UINT partIndex = 0;
		container->FindFirstPartKind(DXC_PART_DXIL, &partIndex) >> dx::dxErrorChecker;
		container->GetPartReflection(partIndex, IID_PPV_ARGS(&outReflector)) >> dx::dxErrorChecker;
		outReflector->GetDesc(&outDesc) >> dx::dxErrorChecker;
	}

	void
	emitType(
		gfx::DynamicConstantBufferDesc& desc,
		ID3D12ShaderReflectionType*     type,
		const D3D12_SHADER_TYPE_DESC&   typeDesc,
		std::string_view                path)
	{
		if (typeDesc.Class == D3D_SVC_STRUCT)
		{
			desc.AddStruct(path);
			for (UINT i = 0; i < typeDesc.Members; ++i)
			{
				auto* memberType = type->GetMemberTypeByIndex(i);
				auto  memberDesc = D3D12_SHADER_TYPE_DESC{};
				memberType->GetDesc(&memberDesc);

				auto fullPath = std::format("{}.{}", path, type->GetMemberTypeName(i));
				emitType(desc, memberType, memberDesc, fullPath);
			}
		}
		else if (typeDesc.Elements > 0)
		{
			if (typeDesc.Class == D3D_SVC_STRUCT)
			{
				desc.AddStructArray(path, typeDesc.Elements);
				auto*                  elemType = type->GetSubType();
				D3D12_SHADER_TYPE_DESC elemDesc{};
				elemType->GetDesc(&elemDesc);

				for (UINT i = 0; i < elemDesc.Members; ++i)
				{
					auto*                  memberType = elemType->GetMemberTypeByIndex(i);
					D3D12_SHADER_TYPE_DESC memberDesc{};
					memberType->GetDesc(&memberDesc);

					auto fullPath = std::format("{}.{}", path, elemType->GetMemberTypeName(i));
					emitType(desc, memberType, memberDesc, fullPath);
				}
			}
			else
			{
				desc.AddElementArray(
					path,
					shaderVarTypeToConstantBufferType(typeDesc),
					typeDesc.Elements);
			}
		}
		else
		{
			desc.AddElement(path, shaderVarTypeToConstantBufferType(typeDesc));
		}
	}

	nvrhi::Format
	mapFormat(const D3D12_SIGNATURE_PARAMETER_DESC& desc)
	{
		uint32_t componentCount = (desc.Mask & 1 ? 1 : 0) + (desc.Mask & 2 ? 1 : 0) +
		                          (desc.Mask & 4 ? 1 : 0) + (desc.Mask & 8 ? 1 : 0);

		if (desc.ComponentType == D3D_REGISTER_COMPONENT_FLOAT32)
		{
			switch (componentCount)
			{
			case 2:
				return nvrhi::Format::RG32_FLOAT;
			case 3:
				return nvrhi::Format::RGB32_FLOAT;
			case 4:
				return nvrhi::Format::RGBA32_FLOAT;
			}
		}

		throw std::runtime_error(
			std::format(
				"Unsupported shader input format: ComponentType={}, Mask={}",
				static_cast<uint32_t>(desc.ComponentType),
				static_cast<uint32_t>(desc.Mask)));
	}
}

namespace gfx
{
	VertexAttribute::Type
	mapSemantic(const char* semantic)
	{
		if (strcmp(semantic, "POSITION") == 0)
			return VertexAttribute::kPosition;
		if (strcmp(semantic, "NORMAL") == 0)
			return VertexAttribute::kNormal;
		if (strcmp(semantic, "TEXCOORD") == 0)
			return VertexAttribute::kUV;
		if (strcmp(semantic, "TANGENT") == 0)
			return VertexAttribute::kTangent;
		return VertexAttribute::kInvalid;
	}

	std::vector<VertexAttribute>
	getVertexAttributes(nvrhi::ShaderHandle shader)
	{
		auto        reflector  = nvrhi::RefCountPtr<ID3D12ShaderReflection>{};
		auto        shaderDesc = D3D12_SHADER_DESC{};
		const void* bytecode;
		size_t      bytecodeSize;
		shader->getBytecode(&bytecode, &bytecodeSize);

		getReflectorAndShaderDescDXIL(
			std::span(reinterpret_cast<const std::byte*>(bytecode), bytecodeSize),
			reflector,
			shaderDesc);

		if (D3D12_SHVER_GET_TYPE(shaderDesc.Version) != D3D12_SHVER_VERTEX_SHADER)
			throw std::runtime_error("Provided shader is not a vertex shader.");

		auto vertexInputs = std::vector<VertexAttribute>{};

		for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
		{
			D3D12_SIGNATURE_PARAMETER_DESC paramDesc{};
			reflector->GetInputParameterDesc(i, &paramDesc);

			if (paramDesc.SystemValueType != D3D_NAME_UNDEFINED)
				continue;

			auto attr = mapSemantic(paramDesc.SemanticName);
			if (attr == VertexAttribute::kInvalid)
				continue;

			vertexInputs.push_back(
				{ attr,
			      paramDesc.SemanticIndex,
			      paramDesc.SemanticName,
			      std::format("{}{}", paramDesc.SemanticName, paramDesc.SemanticIndex),
			      mapFormat(paramDesc) });
		}

		return vertexInputs;
	}

	DynamicConstantBufferDesc
	getDynamicConstantBufferDesc(
		std::span<const std::byte> shaderByteCode,
		uint32_t                   constantBufferSpace,
		uint32_t                   constantBufferSlot)
	{
		auto reflector  = nvrhi::RefCountPtr<ID3D12ShaderReflection>{};
		auto shaderDesc = D3D12_SHADER_DESC{};
		getReflectorAndShaderDescDXIL(shaderByteCode, reflector, shaderDesc);

		auto desc = DynamicConstantBufferDesc{};

		for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
		{
			D3D12_SHADER_INPUT_BIND_DESC bindDesc{};
			reflector->GetResourceBindingDesc(i, &bindDesc);

			if (bindDesc.Type == D3D_SIT_CBUFFER && bindDesc.BindPoint == constantBufferSlot &&
			    bindDesc.Space == constantBufferSpace)
			{
				auto cbReflect = reflector->GetConstantBufferByName(bindDesc.Name);
				auto cbDesc    = D3D12_SHADER_BUFFER_DESC{};
				cbReflect->GetDesc(&cbDesc);

				if (cbDesc.Name)
					desc.SetName(cbDesc.Name);

				for (UINT varIdx = 0; varIdx < cbDesc.Variables; ++varIdx)
				{
					auto* var     = cbReflect->GetVariableByIndex(varIdx);
					auto  varDesc = D3D12_SHADER_VARIABLE_DESC{};
					var->GetDesc(&varDesc);

					auto* type     = var->GetType();
					auto  typeDesc = D3D12_SHADER_TYPE_DESC{};
					type->GetDesc(&typeDesc);

					emitType(desc, type, typeDesc, varDesc.Name);
				}

				break;
			}
		}

		return desc;
	}
}
