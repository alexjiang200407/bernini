#include "uniforms/Uniforms.h"
#include "pipeline/MeshletPipeline.h"
#include "slang/ErrorChecker.h"
#include "uniforms/DescriptorHandle.h"
#include <slang.h>

namespace bgl
{
	namespace detail
	{
		namespace
		{
			TraversalResult
			ReturnNullResult();
		}

		class UniformNullNode final : public UniformsNode
		{
		public:
			UniformNullNode() = default;

			TraversalResult
			Traverse(size_t, const std::string&) override
			{
				return ReturnNullResult();
			}

			TraversalResult
			Traverse(size_t, uint32_t) override
			{
				return ReturnNullResult();
			}

			UniformType
			GetType() const override
			{
				return UniformType::kNull;
			}

			UniformValueType
			GetValueType() const override
			{
				return UniformValueType::kNone;
			}

			size_t
			GetSize() const override
			{
				return 0;
			}
		};

		static UniformNullNode g_UniformNullNode;

		namespace
		{
			TraversalResult
			ReturnNullResult()
			{
				TraversalResult result{};
				result.node           = &g_UniformNullNode;
				result.relativeOffset = 0;
				return result;
			}
		}

		class UniformValueNode final : public UniformsNode
		{
		public:
			explicit UniformValueNode(UniformValueType valueType) : m_ValueType(valueType) {}

			TraversalResult
			Traverse(size_t, const std::string&) override
			{
				return ReturnNullResult();
			}

			TraversalResult
			Traverse(size_t, uint32_t) override
			{
				return ReturnNullResult();
			}

			UniformType
			GetType() const override
			{
				return UniformType::kValue;
			}

			UniformValueType
			GetValueType() const override
			{
				return m_ValueType;
			}

			size_t
			GetSize() const override
			{
				return ValueTypeSize(m_ValueType);
			}

		private:
			UniformValueType m_ValueType;
		};

		class UniformStructNode final : public UniformsNode
		{
		public:
			using MemberMap =
				std::unordered_map<std::string, std::pair<std::unique_ptr<UniformsNode>, size_t>>;

			explicit UniformStructNode(MemberMap members, size_t totalSize) :
				m_Members(std::move(members)), m_TotalSize(totalSize)
			{}

			TraversalResult
			Traverse(size_t currentOffset, const std::string& member) override
			{
				auto it = m_Members.find(member);
				if (it == m_Members.end())
				{
					return ReturnNullResult();
				}
				return { it->second.first.get(), currentOffset + it->second.second };
			}

			TraversalResult
			Traverse(size_t, uint32_t) override
			{
				return ReturnNullResult();
			}

			UniformType
			GetType() const override
			{
				return UniformType::kStruct;
			}

			UniformValueType
			GetValueType() const override
			{
				return UniformValueType::kNone;
			}
			size_t
			GetSize() const override
			{
				return m_TotalSize;
			}

		private:
			MemberMap m_Members;
			size_t    m_TotalSize;
		};

		class UniformArrayNode final : public UniformsNode
		{
		public:
			explicit UniformArrayNode(
				std::unique_ptr<UniformsNode> elementNode,
				size_t                        count,
				size_t                        stride) :
				m_ElementNode(std::move(elementNode)), m_Count(count), m_Stride(stride)
			{}

			UniformArrayNode(const UniformArrayNode&) noexcept = delete;
			UniformArrayNode(UniformArrayNode&&) noexcept      = delete;

			UniformArrayNode&
			operator=(const UniformArrayNode&) noexcept = delete;

			UniformArrayNode&
			operator=(UniformArrayNode&&) noexcept = delete;

			TraversalResult
			Traverse(size_t, const std::string&) override
			{
				return ReturnNullResult();
			}

			TraversalResult
			Traverse(size_t currentOffset, uint32_t idx) override
			{
				if (idx >= m_Count)
					return ReturnNullResult();
				return { m_ElementNode.get(), currentOffset + idx * m_Stride };
			}

			UniformType
			GetType() const override
			{
				return UniformType::kArray;
			}

			UniformValueType
			GetValueType() const override
			{
				return UniformValueType::kNone;
			}

			size_t
			GetSize() const override
			{
				return m_Count * m_Stride;
			}

			size_t
			GetCount() const
			{
				return m_Count;
			}

		private:
			std::unique_ptr<UniformsNode> m_ElementNode;
			size_t                        m_Count;
			size_t                        m_Stride;
		};

	}

	Uniforms::Accessor
	Uniforms::operator[](const std::string& name)
	{
		return Accessor(m_Buffer.data(), 0, m_Root.get())[name];
	}

	Uniforms::Accessor
	Uniforms::operator[](uint32_t idx)
	{
		return Accessor(m_Buffer.data(), 0, m_Root.get())[idx];
	}

	Uniforms::ConstAccessor
	Uniforms::operator[](const std::string& name) const
	{
		return ConstAccessor(m_Buffer.data(), 0, m_Root.get())[name];
	}

	Uniforms::ConstAccessor
	Uniforms::operator[](uint32_t idx) const
	{
		return ConstAccessor(m_Buffer.data(), 0, m_Root.get())[idx];
	}

	Uniforms::Uniforms(IMeshletPipeline const* pipeline)
	{
		size_t totalBufferSize = pipeline->GetUniformSize();
		auto*  structLayout    = pipeline->GetUniformLayout();

		m_Size = totalBufferSize;

		gassert(structLayout != nullptr, "Pipeline must have a valid uniform layout");

		m_Root = BuildNode(structLayout);
		m_Buffer.resize(totalBufferSize, std::byte{ 0 });
	}

	UniformValueType
	Uniforms::ResolveScalarType(slang::TypeReflection* type)
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

	std::unique_ptr<detail::UniformsNode>
	Uniforms::BuildNode(slang::TypeLayoutReflection* typeLayout)
	{
		using Kind = slang::TypeReflection::Kind;

		switch (typeLayout->getKind())
		{
		case Kind::Struct:
		{
			detail::UniformStructNode::MemberMap members;
			uint32_t                             fieldCount = typeLayout->getFieldCount();

			for (uint32_t i = 0; i < fieldCount; ++i)
			{
				slang::VariableLayoutReflection* field          = typeLayout->getFieldByIndex(i);
				size_t                           relativeOffset = field->getOffset();
				std::string                      fieldName      = field->getName();

				members[fieldName] = { BuildNode(field->getTypeLayout()), relativeOffset };
			}

			return std::make_unique<detail::UniformStructNode>(
				std::move(members),
				typeLayout->getSize());
		}

		case Kind::Array:
		{
			return std::make_unique<detail::UniformArrayNode>(
				BuildNode(typeLayout->getElementTypeLayout()),
				typeLayout->getElementCount(),
				typeLayout->getStride());
		}

		case Kind::Scalar:
		case Kind::Vector:
		case Kind::Matrix:
		{
			return std::make_unique<detail::UniformValueNode>(
				ResolveScalarType(typeLayout->getType()));
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
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::Resource);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::SamplerState);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::ShaderStorageBuffer);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::Specialized);
			HANDLE_UNSUPPORTED_TYPE_KIND(Kind::TextureBuffer);

		default:
			gfatal("Unsupported type kind in push constants");
		}
	}
}
