#pragma once
#include "constants/constants.h"
#include "resource/Buffer.h"
#include "resource/Sampler.h"
#include "resource/Shader.h"
#include "resource/Texture.h"
#include "uniforms/DescriptorHandle.h"
#include "uniforms/ReflectedLayout.h"
#include "uniforms/UniformValueType.h"
#include <core/err/util.h>
#include <core/ref/Ref.h>
#include <core/ref/SharedRef.h>

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

		template <typename...>
		inline constexpr bool always_false = false;

		template <typename T>
		UniformValueType
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
				static_assert(always_false<T>, "Unsupported uniform type T");
		}
	}

	/**
	 * The CPU mirror of one constant buffer, addressed by member name or declaration index.
	 *
	 * A bindless handle assigned through an accessor is not written verbatim. What a shader has to
	 * find in the cbuffer to reach the resource is the backend's business -- a descriptor index
	 * under a directly-indexed heap, a pool slot where the value is patched at dispatch -- so the
	 * translation is `ResolveBindless`, which each backend implements. Everything else, the layout
	 * tree and the flat byte mirror, is shared.
	 *
	 * Obtained from IDevice::CreateUniforms; it cannot be constructed backend-agnostically.
	 */
	class IUniforms : public core::Ref
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
				return AccessorBase(m_Owner, m_Data, offset, node);
			}

			AccessorBase
			operator[](uint32_t idx) const
			{
				auto [node, offset] = m_Node->Traverse(m_Offset, idx);
				return AccessorBase(m_Owner, m_Data, offset, node);
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

			template <typename T>
			explicit
			operator T() const
			{
				AssertIsValue();
				AssertType<T>();

				T value{};
				std::memcpy(&value, static_cast<const uint8_t*>(m_Data) + m_Offset, sizeof(T));
				return value;
			}

			template <typename T>
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
							(*this)[key] = m_Owner->ResolveBindless(handle);
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
					*this = m_Owner->ResolveBindless(handle);
					return *this;
				}

				core::throw_runtime_error(
					"Accessor at offset {} cannot be assigned with buffer handle",
					m_Offset);
			}

			AccessorBase&
			operator=(SamplerHandle handle)
			{
				if (GetType() == UniformType::kValue &&
				    m_Node->GetValueType() == UniformValueType::kDescriptorHandle)
				{
					*this = m_Owner->ResolveBindless(handle);
					return *this;
				}

				core::throw_runtime_error(
					"Accessor at offset {} cannot be assigned with sampler handle",
					m_Offset);
			}

			AccessorBase&
			operator=(TextureHandle handle)
			{
				if (GetType() == UniformType::kStruct && (*this)[c_HandleUniformMember].IsValid())
				{
					(*this)[c_HandleUniformMember] = m_Owner->ResolveBindless(handle);
					return *this;
				}

				core::throw_runtime_error(
					"Accessor at offset {} cannot be assigned with texture handle",
					m_Offset);
			}

			AccessorBase&
			operator=(TextureAssetHandle handle)
			{
				if (GetType() == UniformType::kStruct && (*this)[c_HandleUniformMember].IsValid())
				{
					(*this)[c_HandleUniformMember] = m_Owner->ResolveBindless(handle);
					return *this;
				}

				core::throw_runtime_error(
					"Accessor at offset {} cannot be assigned with texture asset handle",
					m_Offset);
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

			template <typename T>
			void
			operator=(T value) const
			{
				AssertIsValue();
				AssertType<T>();

				std::memcpy(static_cast<uint8_t*>(m_Data) + m_Offset, &value, sizeof(T));
			}

		private:
			AccessorBase(
				const IUniforms*      owner,
				DataPtr               data,
				size_t                offset,
				detail::UniformsNode* node) :
				m_Owner(owner), m_Data(data), m_Offset(offset), m_Node(node)
			{}

			void
			AssertIsValue() const
			{
				if (!m_Node || m_Node->GetType() != UniformType::kValue)
					core::throw_runtime_error("Uniforms::Accessor: node is not a value type");
			}

			template <typename T>
			void
			AssertType() const
			{
				if (m_Node->GetValueType() != detail::ValueMap<T>())
					core::throw_runtime_error("Uniforms::Accessor: type mismatch");
			}

		private:
			const IUniforms*      m_Owner;
			DataPtr               m_Data;
			size_t                m_Offset;
			detail::UniformsNode* m_Node;

			friend class IUniforms;
		};

	public:
		using Accessor      = AccessorBase<void*>;
		using ConstAccessor = AccessorBase<const void*>;

	public:
		/**
		 * What the shader must find in the constant buffer to reach the handle's resource.
		 *
		 * The two backends disagree: D3D12 needs the resource's index into the shader-visible
		 * descriptor heap, Metal the pool slot it patches to a native address at dispatch. Both
		 * happen to be a uint32 today, which is why the return is a DescriptorHandle rather than
		 * one -- a backend that can write the final native value here is free to.
		 */
		[[nodiscard]] virtual DescriptorHandle
		ResolveBindless(const BufferHandle& handle) const noexcept = 0;

		[[nodiscard]] virtual DescriptorHandle
		ResolveBindless(const SamplerHandle& handle) const noexcept = 0;

		[[nodiscard]] virtual DescriptorHandle
		ResolveBindless(const TextureHandle& handle) const noexcept = 0;

		[[nodiscard]] DescriptorHandle
		ResolveBindless(const TextureAssetHandle& handle) const noexcept
		{
			return ResolveBindless(TextureHandle::From(handle));
		}

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

	protected:
		IUniforms(IMeshletPipeline const* pipeline, std::string_view cbufferName);
		IUniforms(IComputePipeline const* pipeline, std::string_view cbufferName);

	private:
		static std::unique_ptr<detail::UniformsNode>
		BuildNode(const ReflectedLayout& layout);

	private:
		std::unique_ptr<detail::UniformsNode> m_Root           = nullptr;
		size_t                                m_Size           = 0;
		uint32_t                              m_RootParamIndex = 0xFFFFFFFF;

		// flat CPU-side mirror
		std::vector<std::byte> m_Buffer;
	};

	using UniformsRef = core::SharedRef<IUniforms>;
}
