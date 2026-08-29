#pragma once
#include "constants/constants.h"
#include "resource/Buffer.h"
#include "resource/Sampler.h"
#include "resource/Shader.h"
#include "resource/Srv.h"
#include "resource/Texture.h"
#include "uniforms/DescriptorHandle.h"
#include "uniforms/ReflectedLayout.h"
#include "uniforms/UniformLayoutEntry.h"
#include "uniforms/UniformValueType.h"
#include <core/err/util.h>

namespace bgl
{
	class IMeshletPipeline;
	class IComputePipeline;

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
		ValueMap()
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
			else if constexpr (std::is_same_v<T, glm::uvec2> || std::is_same_v<T, DescriptorHandle>)
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
				return ValueMap<std::underlying_type_t<T>>();
			else
				return UniformValueType::kNone;
		}

		/** A type the mirror stores as a value: a scalar, a glm vector or matrix, an enum, a handle. */
		template <typename T>
		concept UniformValue = ValueMap<T>() != UniformValueType::kNone;
	}

	/** A type one of `Uniforms::AccessorBase`'s assignment operators accepts. */
	template <typename T>
	concept UniformAssignable = detail::UniformValue<T> || std::same_as<T, BufferHandle> ||
	                            std::same_as<T, SamplerHandle> || std::same_as<T, SrvHandle> ||
	                            std::same_as<T, TextureAssetHandle>;

	class Uniforms final
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

			AccessorBase&
			operator=(BufferHandle handle)
			{
				if (GetType() == UniformType::kStruct &&
				    m_Node->GetSize() == detail::ValueTypeSize(UniformValueType::kDescriptorHandle))
				{
					for (const auto key : c_SmartBufferUniformIndices)
					{
						if ((*this)[key].IsValid())
						{
							(*this)[key] = DescriptorHandle(handle.bindlessIndex);
							return *this;
						}
					}

					core::throw_runtime_error(
						"Accessor at offset {} is not a valid buffer",
						m_Offset);
				}
				else if (
					GetType() == UniformType::kValue &&
					m_Node->GetValueType() == UniformValueType::kDescriptorHandle)
				{
					*this = DescriptorHandle(handle.bindlessIndex);
					return *this;
				}

				core::throw_runtime_error(
					"Accessor at offset {} cannot be assigned with buffer handle",
					m_Offset);
			}

			AccessorBase&
			operator=(BufferSrvHandle handle)
			{
				// Only the descriptor travels: this handle's slot indexes the view pool, not the
				// buffer pool, and naming it as a buffer's would be a lie waiting to be believed.
				return *this = BufferHandle{ {}, handle.bindlessIndex };
			}

			AccessorBase&
			operator=(SamplerHandle handle)
			{
				if (GetType() == UniformType::kValue &&
				    m_Node->GetValueType() == UniformValueType::kDescriptorHandle)
				{
					*this = DescriptorHandle(handle.bindlessIndex);
					return *this;
				}

				core::throw_runtime_error(
					"Accessor at offset {} cannot be assigned with sampler handle",
					m_Offset);
			}

			AccessorBase&
			operator=(SrvHandle handle)
			{
				if (GetType() == UniformType::kStruct && (*this)[c_HandleUniformMember].IsValid())
				{
					(*this)[c_HandleUniformMember] = DescriptorHandle(handle.bindlessIndex);
					return *this;
				}

				core::throw_runtime_error(
					"Accessor at offset {} cannot be assigned with SRV handle",
					m_Offset);
			}

			AccessorBase&
			operator=(TextureAssetHandle handle)
			{
				if (GetType() == UniformType::kStruct && (*this)[c_HandleUniformMember].IsValid())
				{
					(*this)[c_HandleUniformMember] = DescriptorHandle(handle.srvBindlessIndex);
					return *this;
				}

				core::throw_runtime_error(
					"Accessor at offset {} cannot be assigned with texture asset handle",
					m_Offset);
			}

			/**
			 * Assigns `value` when this accessor resolves to a declared member, and does nothing
			 * when it does not.
			 *
			 * The spelling for a member a PSO variant may not declare. Silence is the point and the
			 * hazard: a name no variant declares reads the same as a field this one omits, so a
			 * binder using this must resolve its names once through `FindUnknownMembers`.
			 */
			template <UniformAssignable T>
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
					core::throw_runtime_error("Uniforms::Accessor: node is not a value type");
			}

			template <detail::UniformValue T>
			void
			AssertType() const
			{
				if (m_Node->GetValueType() != detail::ValueMap<T>())
					core::throw_runtime_error("Uniforms::Accessor: type mismatch");
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
		Uniforms(IMeshletPipeline const* pipeline, std::string_view cbufferName);
		Uniforms(IComputePipeline const* pipeline, std::string_view cbufferName);

		Uniforms(const Uniforms&) = delete;
		Uniforms(Uniforms&&)      = default;

		Uniforms&
		operator=(Uniforms&&) = default;

		Uniforms&
		operator=(const Uniforms&) = delete;

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

		[[nodiscard]] uint32_t
		GetRootParamIndex() const
		{
			return m_RootParamIndex;
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
		void
		Adopt(UniformLayoutEntry entry);

		static std::unique_ptr<detail::UniformsNode>
		BuildNode(const ReflectedLayout& layout);

	private:
		std::unique_ptr<detail::UniformsNode>  m_Root = nullptr;
		std::shared_ptr<const ReflectedLayout> m_Layout;
		size_t                                 m_Size           = 0;
		uint32_t                               m_RootParamIndex = 0xFFFFFFFF;

		// flat CPU-side mirror
		std::vector<std::byte> m_Buffer;
	};

	/**
	 * The names that resolve in none of `variants` -- the members a binder names but no PSO in the
	 * family declares.
	 *
	 * A name absent from one variant is ordinary: variants declare different subsets of a constant
	 * buffer, which is what a reflected mirror exists to absorb. A name absent from *every* variant
	 * is a typo or a shader rename, and is the case `Accessor::IsValid()` cannot distinguish on its
	 * own. Call it once when the family is built, not per draw.
	 *
	 * @param variants One family's mirrors of the same constant buffer. Null and empty entries are
	 *                 skipped, so a variant that does not declare the buffer at all costs nothing.
	 * @return The offending names, in the order given. Empty when every name resolves somewhere.
	 */
	[[nodiscard]] std::vector<std::string_view>
	FindUnknownMembers(
		std::span<const Uniforms* const>  variants,
		std::span<const std::string_view> names);
}
