#pragma once
#include <algorithm>
#include <bgl_common/ReflectedLayout.h>
#include <bgl_common/UniformValueType.h>
#include <concepts>
#include <core/err/util.h>
#include <core/glm.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace bgl
{
	namespace detail
	{
		class UniformsNode;

		struct TraversalResult
		{
			UniformsNode* node;
			size_t        absoluteOffset;

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
			case UniformValueType::kNone:
			default:
				return 0;
			}
		}

		class UniformsNode
		{
		public:
			virtual ~UniformsNode() noexcept = default;

			virtual TraversalResult
			Traverse(size_t currentOffset, std::string_view member) = 0;

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
		constexpr UniformValueType
		NeutralValueMap()
		{
			if constexpr (std::is_same_v<T, float>)
				return UniformValueType::kFloat;
			else if constexpr (std::is_same_v<T, glm::vec2>)
				return UniformValueType::kFloat2;
			else if constexpr (std::is_same_v<T, glm::vec3>)
				return UniformValueType::kFloat3;
			else if constexpr (std::is_same_v<T, glm::vec4>)
				return UniformValueType::kFloat4;
			else if constexpr (std::is_same_v<T, int32_t>)
				return UniformValueType::kInt;
			else if constexpr (std::is_same_v<T, glm::ivec2>)
				return UniformValueType::kInt2;
			else if constexpr (std::is_same_v<T, glm::ivec3>)
				return UniformValueType::kInt3;
			else if constexpr (std::is_same_v<T, glm::ivec4>)
				return UniformValueType::kInt4;
			else if constexpr (std::is_same_v<T, uint32_t>)
				return UniformValueType::kUInt;
			else if constexpr (std::is_same_v<T, glm::uvec2>)
				return UniformValueType::kUInt2;
			else if constexpr (std::is_same_v<T, glm::uvec3>)
				return UniformValueType::kUInt3;
			else if constexpr (std::is_same_v<T, glm::uvec4>)
				return UniformValueType::kUInt4;
			else if constexpr (std::is_same_v<T, bool>)
				return UniformValueType::kBool;
			else if constexpr (std::is_same_v<T, glm::mat4>)
				return UniformValueType::kMat4x4;
			else if constexpr (std::is_enum_v<T>)
				return NeutralValueMap<std::underlying_type_t<T>>();
			else
				return UniformValueType::kNone;
		}
	}

	/**
	 * The value type the mirror stores a C++ type as, `kNone` for one it does not store. The scalars,
	 * the glm vectors and matrix, and enums over them are mapped here; a renderer maps its own types
	 * by specialising this. The descriptor handle is one: its alignment is the backend's, so the
	 * layout walk never names it.
	 */
	template <typename T>
	struct UniformValueMap
	{
		static constexpr UniformValueType c_Value = detail::NeutralValueMap<T>();
	};

	namespace detail
	{
		/** A type the mirror stores as a value: a scalar, a glm vector or matrix, an enum, a handle. */
		template <typename T>
		concept UniformValue = UniformValueMap<T>::c_Value != UniformValueType::kNone;
	}

	/**
	 * How a renderer's own types are written into a mirror. `uniforms["x"] = handle` for a type the
	 * mirror does not store as a value resolves here, so a renderer specialises it per handle type
	 * with `static void Assign(UniformsBase::Accessor, const T&)`, written over the accessor's
	 * `AssignDescriptorIndex`. Left undefined so that a type nobody maps is not assignable.
	 */
	template <typename T>
	struct UniformAssign;

	/**
	 * The CPU side of one constant buffer: a flat byte mirror and the reflected tree that addresses
	 * it, so `mirror["member"] = value` writes where the shader reads. Knows values and descriptor
	 * indices, and nothing of what a descriptor names; a renderer extends it with its handle types
	 * through `UniformAssign` and `UniformValueMap` (docs/uniforms.md).
	 */
	class UniformsBase
	{
	public:
		template <typename DataPtr>
		class AccessorBase
		{
		public:
			AccessorBase
			operator[](std::string_view name) const
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

			[[nodiscard]] bool
			IsNull() const
			{
				return m_Node->GetType() == UniformType::kNull;
			}

			[[nodiscard]] bool
			IsValid() const
			{
				return m_Node != nullptr && m_Node->GetType() != UniformType::kNull;
			}

			template <detail::UniformValue T>
			explicit
			operator T() const
			{
				AssertIsValue();
				AssertType<T>();

				T value{};
				std::memcpy(&value, static_cast<const uint8_t*>(m_Data) + m_Offset, sizeof(T));
				return value;
			}

			template <detail::UniformValue T>
			bool
			operator==(const T& val) const
			{
				return val == static_cast<T>(*this);
			}

			template <typename T>
				requires requires(AccessorBase accessor, const T& v) {
					UniformAssign<T>::Assign(accessor, v);
				}
			AccessorBase&
			operator=(const T& value)
			{
				UniformAssign<T>::Assign(*this, value);
				return *this;
			}

			/**
			 * Writes `index` where the shader reads a descriptor: the one primitive every handle
			 * assignment a renderer defines reduces to. What the index means is the renderer's --
			 * a heap slot, a pool slot -- and the mirror only places it.
			 *
			 * @throws std::runtime_error when this member is not a descriptor value.
			 */
			void
			AssignDescriptorIndex(uint32_t index) const
			{
				if (GetType() != UniformType::kValue ||
				    m_Node->GetValueType() != UniformValueType::kDescriptorHandle)
				{
					core::throw_runtime_error(
						"Accessor at offset {} is not a descriptor value",
						m_Offset);
				}

				const uint32_t words[2] = { index, 0 };
				std::memcpy(static_cast<uint8_t*>(m_Data) + m_Offset, words, sizeof(words));
			}

			/**
			 * Assigns `value` when this accessor resolves to a declared member, and does nothing
			 * when it does not.
			 *
			 * The spelling for a member a PSO variant may not declare. Silence is the point and the
			 * hazard: a name no variant declares reads the same as a field this one omits, so a
			 * binder using this must resolve its names once through `FindUnknownMembers`.
			 */
			template <typename T>
				requires requires(AccessorBase accessor, const T& v) { accessor = v; }
			void
			SetIfValid(const T& value)
			{
				if (IsValid())
				{
					*this = value;
				}
			}

			UniformType
			GetType() const
			{
				return m_Node->GetType();
			}

			UniformValueType
			GetValueType() const
			{
				return m_Node->GetValueType();
			}

			/** The member's size in bytes: a value's, a struct's whole, an array's count times stride. */
			size_t
			GetSize() const
			{
				return m_Node->GetSize();
			}

			size_t
			GetOffset() const
			{
				return m_Offset;
			}

			template <detail::UniformValue T>
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
				if (!m_Node || m_Node->GetType() != UniformType::kValue)
					core::throw_runtime_error("UniformsBase::Accessor: node is not a value type");
			}

			template <detail::UniformValue T>
			void
			AssertType() const
			{
				if (m_Node->GetValueType() != UniformValueMap<T>::c_Value)
					core::throw_runtime_error("UniformsBase::Accessor: type mismatch");
			}

		private:
			DataPtr               m_Data;
			size_t                m_Offset;
			detail::UniformsNode* m_Node;

			friend class UniformsBase;
		};

	public:
		using Accessor      = AccessorBase<void*>;
		using ConstAccessor = AccessorBase<const void*>;

	public:
		UniformsBase() = default;

		/**
		 * Lays `size` zeroed bytes out over `layout`.
		 *
		 * @param layout The reflected tree; shared, since one PSO's layout serves every kernel on it.
		 * @param size The constant buffer's size in bytes, which the tree alone does not carry.
		 */
		UniformsBase(std::shared_ptr<const ReflectedLayout> layout, size_t size);

		UniformsBase(const UniformsBase&) = delete;
		UniformsBase(UniformsBase&&)      = default;

		UniformsBase&
		operator=(UniformsBase&&) = default;

		UniformsBase&
		operator=(const UniformsBase&) = delete;

		Accessor
		operator[](std::string_view name);

		Accessor
		operator[](uint32_t idx);

		ConstAccessor
		operator[](std::string_view name) const;

		ConstAccessor
		operator[](uint32_t idx) const;

		[[nodiscard]] bool
		IsEmpty() const
		{
			return m_Root == nullptr;
		}

		/** Whether `name` resolves to a member of this constant buffer. False for an empty mirror. */
		[[nodiscard]] bool
		HasMember(std::string_view name) const;

		/** The reflected layout this mirror was built from. Null for an empty mirror. */
		[[nodiscard]] const ReflectedLayout*
		GetLayout() const
		{
			return m_Layout.get();
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

		void
		Reset()
		{
			m_Buffer.clear();
			m_Root.reset();
			m_Size = 0;
		}

	private:
		static std::unique_ptr<detail::UniformsNode>
		BuildNode(const ReflectedLayout& layout);

	private:
		std::unique_ptr<detail::UniformsNode>  m_Root = nullptr;
		std::shared_ptr<const ReflectedLayout> m_Layout;
		size_t                                 m_Size = 0;

		// flat CPU-side mirror
		std::vector<std::byte> m_Buffer;
	};

	/** A type one of `UniformsBase::Accessor`'s assignments accepts. */
	template <typename T>
	concept UniformAssignable =
		requires(UniformsBase::Accessor accessor, const T& v) { accessor = v; };

	/**
	 * The names that resolve in none of `variants` -- the members a binder names but no PSO in the
	 * family declares.
	 *
	 * A name absent from one variant is ordinary: variants declare different subsets of a constant
	 * buffer, which is what a reflected mirror exists to absorb. A name absent from *every* variant
	 * is a typo or a shader rename, and is the case `Accessor::IsValid()` cannot distinguish on its
	 * own. Call it once when the family is built, not per draw.
	 *
	 * @param variants One family's mirrors of the same constant buffer, as pointers to any mirror
	 *                 type. Null and empty entries are skipped, so a variant that does not declare
	 *                 the buffer at all costs nothing.
	 * @return The offending names, in the order given. Empty when every name resolves somewhere.
	 */
	template <std::ranges::input_range Variants>
		requires std::convertible_to<std::ranges::range_value_t<Variants>, const UniformsBase*>
	[[nodiscard]] std::vector<std::string_view>
	FindUnknownMembers(const Variants& variants, std::span<const std::string_view> names)
	{
		std::vector<std::string_view> unknown;

		for (const std::string_view name : names)
		{
			const bool knownSomewhere =
				std::ranges::any_of(variants, [name](const UniformsBase* variant) {
					return variant != nullptr && variant->HasMember(name);
				});

			if (!knownSomewhere)
				unknown.push_back(name);
		}

		return unknown;
	}
}
