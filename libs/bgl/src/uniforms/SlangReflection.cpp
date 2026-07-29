#include "uniforms/SlangReflection.h"
#include <slang.h>

namespace bgl
{
	ResourceBinding
	ResolveSlangResourceBinding(slang::TypeReflection* type)
	{
		using Kind = slang::TypeReflection::Kind;

		if (type->getKind() == Kind::SamplerState)
			return ResourceBinding::kSampler;

		// A ShaderStorageBuffer reports no shape at all; a Resource carries one, with the array /
		// multisample / shadow flags above the base-shape bits.
		if (type->getKind() == Kind::ShaderStorageBuffer)
			return ResourceBinding::kBuffer;

		switch (type->getResourceShape() & SLANG_RESOURCE_BASE_SHAPE_MASK)
		{
		case SLANG_TEXTURE_1D:
		case SLANG_TEXTURE_2D:
		case SLANG_TEXTURE_3D:
		case SLANG_TEXTURE_CUBE:
			return ResourceBinding::kTexture;
		case SLANG_STRUCTURED_BUFFER:
		case SLANG_BYTE_ADDRESS_BUFFER:
			return ResourceBinding::kBuffer;
		default:
			gfatal("Unsupported resource shape {}", static_cast<int>(type->getResourceShape()));
		}
	}

	UniformValueType
	ResolveSlangValueType(slang::TypeReflection* type)
	{
		auto kind = type->getKind();

		if (kind == slang::TypeReflection::Kind::Matrix)
		{
			gassert(
				type->getRowCount() == 4 && type->getColumnCount() == 4,
				"Only float mat4x4 supported for now");
			return UniformValueType::kMat4x4;
		}

		uint32_t               componentCount = 1;
		slang::TypeReflection* scalarType     = type;

		if (kind == slang::TypeReflection::Kind::Vector)
		{
			componentCount = static_cast<uint32_t>(type->getElementCount());
			scalarType     = type->getElementType();
		}

		auto scalarKind = scalarType->getScalarType();
		bool isFloat    = scalarKind == slang::TypeReflection::ScalarType::Float32;
		bool isInt      = scalarKind == slang::TypeReflection::ScalarType::Int32;
		bool isUInt     = scalarKind == slang::TypeReflection::ScalarType::UInt32;
		bool isBool     = scalarKind == slang::TypeReflection::ScalarType::Bool;

		if (isBool)
			return UniformValueType::kBool;

		if (isFloat)
		{
			switch (componentCount)
			{
			case 1:
				return UniformValueType::kFloat;
			case 2:
				return UniformValueType::kFloat2;
			case 3:
				return UniformValueType::kFloat3;
			case 4:
				return UniformValueType::kFloat4;
			}
		}

		if (isInt)
		{
			switch (componentCount)
			{
			case 1:
				return UniformValueType::kInt;
			case 2:
				return UniformValueType::kInt2;
			case 3:
				return UniformValueType::kInt3;
			case 4:
				return UniformValueType::kInt4;
			}
		}

		if (isUInt)
		{
			switch (componentCount)
			{
			case 1:
				return UniformValueType::kUInt;
			case 2:
				return UniformValueType::kUInt2;
			case 3:
				return UniformValueType::kUInt3;
			case 4:
				return UniformValueType::kUInt4;
			}
		}

		gfatal("Unsupported scalar/vector type in push constants");
	}

#define HANDLE_UNSUPPORTED_TYPE_KIND(kind) \
	case kind:                             \
		gfatal("Unsupported type kind in push constants: " #kind)

	ReflectedLayout
	ReflectLayoutFromSlang(slang::TypeLayoutReflection* typeLayout)
	{
		using Kind = slang::TypeReflection::Kind;

		ReflectedLayout result;

		switch (typeLayout->getKind())
		{
		case Kind::Struct:
		{
			result.kind = UniformType::kStruct;
			result.size = static_cast<uint32_t>(typeLayout->getSize());

			uint32_t fieldCount = typeLayout->getFieldCount();
			result.fields.reserve(fieldCount);

			for (uint32_t i = 0; i < fieldCount; ++i)
			{
				slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);

				ReflectedField reflected;
				reflected.name   = field->getName();
				reflected.offset = static_cast<uint32_t>(field->getOffset());
				reflected.layout = ReflectLayoutFromSlang(field->getTypeLayout());
				result.fields.push_back(std::move(reflected));
			}

			return result;
		}

		case Kind::Array:
		{
			result.kind        = UniformType::kArray;
			result.arrayCount  = static_cast<uint32_t>(typeLayout->getElementCount());
			result.arrayStride = static_cast<uint32_t>(typeLayout->getStride());
			result.element.push_back(ReflectLayoutFromSlang(typeLayout->getElementTypeLayout()));
			result.size = result.arrayCount * result.arrayStride;
			return result;
		}

		case Kind::Scalar:
		case Kind::Vector:
		case Kind::Matrix:
		{
			result.kind      = UniformType::kValue;
			result.valueType = ResolveSlangValueType(typeLayout->getType());
			result.size      = static_cast<uint32_t>(typeLayout->getSize());
			return result;
		}

		case Kind::Resource:
		case Kind::SamplerState:
		{
			// Metal reflects a bindless handle as a Resource/SamplerState (D3D12 emits a uint2). Lower
			// it to the same 8-byte kDescriptorHandle either backend writes. DXIL never reaches here.
			result.kind            = UniformType::kValue;
			result.valueType       = UniformValueType::kDescriptorHandle;
			result.size            = 8;  // two uint32 -- a resource id / device pointer
			result.resourceBinding = ResolveSlangResourceBinding(typeLayout->getType());
			return result;
		}

			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::ConstantBuffer);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::DynamicResource);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::Enum);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::Feedback);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::GenericTypeParameter);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::Interface);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::MeshOutput);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::None);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::OutputStream);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::ParameterBlock);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::Pointer);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::ShaderStorageBuffer);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::Specialized);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::TextureBuffer);

		default:
			gfatal("Unsupported type kind in push constants");
		}
	}
}
