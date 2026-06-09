#pragma once
#include "resource/Shader.h"
#include "uniforms/DescriptorHandle.h"

namespace slang
{
	class ISession;
}

namespace bgl
{
	class IGraphicsPipeline;

	namespace detail
	{
		class UniformsNode;

		enum class UniformType
		{
			kArray,
			kStruct,
			kValue
		};

		enum class UniformValueType
		{
			kInt,
			kInt2,
			kInt3,
			kInt4,
			kUInt,
			kUInt2,
			kUInt3,
			kUInt4,
			kFloat,
			kFloat2,
			kFloat3,
			kFloat4,
			kBool,
			kMat4x4,
			kNone,
		};

		struct TraversalResult
		{
			UniformsNode* node;
			size_t        relativeOffset;

			[[nodiscard]] bool
			IsValid() const
			{
				return node != nullptr;
			}
		};

		constexpr size_t
		ValueTypeSize(UniformValueType type)
		{
			switch (type)
			{
			case UniformValueType::kInt:
			case UniformValueType::kUInt:
				return sizeof(int32_t);
			case UniformValueType::kInt2:
			case UniformValueType::kUInt2:
				return sizeof(int32_t) * 2;
			case UniformValueType::kInt3:
			case UniformValueType::kUInt3:
				return sizeof(int32_t) * 3;
			case UniformValueType::kInt4:
			case UniformValueType::kUInt4:
				return sizeof(int32_t) * 4;
			case UniformValueType::kFloat:
				return sizeof(float);
			case UniformValueType::kFloat2:
				return sizeof(float) * 2;
			case UniformValueType::kFloat3:
				return sizeof(float) * 3;
			case UniformValueType::kFloat4:
				return sizeof(float) * 4;
			case UniformValueType::kBool:
				return sizeof(bool);
			case UniformValueType::kMat4x4:
				return sizeof(float) * 16;
			default:
				return 0;
			}
		}

		class UniformsNode
		{
		public:
			virtual ~UniformsNode() noexcept = default;

			virtual TraversalResult
			Traverse(size_t currentOffset, const std::string& member) = 0;

			virtual TraversalResult
			Traverse(size_t currentOffset, uint32_t idx) = 0;

			virtual UniformType
			GetType() const = 0;

			virtual UniformValueType
			GetValueType() const = 0;

			virtual size_t
			GetSize() const = 0;
		};

		template <typename T>
		detail::UniformValueType
		ValueMap()
		{
			if constexpr (std::is_same_v<T, float>)
				return detail::UniformValueType::kFloat;
			else if constexpr (std::is_same_v<T, glm::vec2>)
				return detail::UniformValueType::kFloat2;
			else if constexpr (std::is_same_v<T, glm::vec3>)
				return detail::UniformValueType::kFloat3;
			else if constexpr (std::is_same_v<T, glm::vec4>)
				return detail::UniformValueType::kFloat4;
			else if constexpr (std::is_same_v<T, int32_t>)
				return detail::UniformValueType::kInt;
			else if constexpr (std::is_same_v<T, glm::ivec2>)
				return detail::UniformValueType::kInt2;
			else if constexpr (std::is_same_v<T, glm::ivec3>)
				return detail::UniformValueType::kInt3;
			else if constexpr (std::is_same_v<T, glm::ivec4>)
				return detail::UniformValueType::kInt4;
			else if constexpr (std::is_same_v<T, uint32_t>)
				return detail::UniformValueType::kUInt;
			else if constexpr (std::is_same_v<T, glm::uvec2> || std::is_same_v<T, DescriptorHandle>)
				return detail::UniformValueType::kUInt2;
			else if constexpr (std::is_same_v<T, glm::uvec3>)
				return detail::UniformValueType::kUInt3;
			else if constexpr (std::is_same_v<T, glm::uvec4>)
				return detail::UniformValueType::kUInt4;
			else if constexpr (std::is_same_v<T, bool>)
				return detail::UniformValueType::kBool;
			else if constexpr (std::is_same_v<T, glm::mat4>)
				return detail::UniformValueType::kMat4x4;
			else
				static_assert(false, "Unsupported uniform type T");
		}
	}

	class Uniforms final
	{
	private:
		template <typename DataPtr>
		class AccessorBase
		{
		public:
			AccessorBase
			operator[](const std::string& name) const
			{
				auto [node, offset] = m_Node->Traverse(m_Offset, name);
				return AccessorBase(m_Data, offset, node);
			}

			AccessorBase
			operator[](uint32_t idx) const
			{
				auto [node, offset] = m_Node->Traverse(m_Offset, idx);
				return AccessorBase(m_Data, offset, node);
			}

			template <typename T>
			operator T() const
			{
				AssertIsValue();
				AssertType<T>();

				T value{};
				std::memcpy(&value, static_cast<const uint8_t*>(m_Data) + m_Offset, sizeof(T));
				return value;
			}

			template <typename T>
			void
			operator=(T value) const
			{
				AssertIsValue();
				AssertType<T>();

				std::memcpy(static_cast<uint8_t*>(m_Data) + m_Offset, &value, sizeof(T));
			}

		private:
			AccessorBase(DataPtr data, size_t offset, detail::UniformsNode* node) :
				m_Data(data), m_Offset(offset), m_Node(node)
			{}

			void
			AssertIsValue() const
			{
				if (!m_Node || m_Node->GetType() != detail::UniformType::kValue)
					throw std::runtime_error("Uniforms::Accessor: node is not a value type");
			}

			template <typename T>
			void
			AssertType() const
			{
				if (m_Node->GetValueType() != detail::ValueMap<T>())
					throw std::runtime_error("Uniforms::Accessor: type mismatch");
			}

		private:
			DataPtr               m_Data;
			size_t                m_Offset;
			detail::UniformsNode* m_Node;

			friend class Uniforms;
		};

	public:
		using Accessor      = AccessorBase<void*>;
		using ConstAccessor = AccessorBase<const void*>;

	public:
		Uniforms() = default;
		Uniforms(IGraphicsPipeline const* pipeline);
		Uniforms(const Uniforms&) = delete;
		Uniforms(Uniforms&&)      = default;

		Uniforms&
		operator=(Uniforms&&) = default;

		Uniforms&
		operator=(const Uniforms&) = delete;

		Accessor
		operator[](const std::string& name);

		Accessor
		operator[](uint32_t idx);

		ConstAccessor
		operator[](const std::string& name) const;

		ConstAccessor
		operator[](uint32_t idx) const;

		[[nodiscard]] bool
		IsEmpty() const
		{
			return m_Root == nullptr;
		}

		[[nodiscard]] size_t
		GetSize() const
		{
			return m_Size;
		}

		[[nodiscard]] const void*
		Data() const
		{
			return m_Buffer.data();
		}

	private:
		static detail::UniformValueType
		ResolveScalarType(slang::TypeReflection* type);

		static std::unique_ptr<detail::UniformsNode>
		BuildNode(slang::TypeLayoutReflection* typeLayout);

	private:
		std::unique_ptr<detail::UniformsNode> m_Root = nullptr;
		size_t                                m_Size = 0;

		// flat CPU-side mirror
		std::vector<std::byte> m_Buffer;
	};
}
