#pragma once
#include "ElementType.h"
#include "GfxException.h"
#include "buffer/DynamicConstantBufferDesc.h"
#include <core/EnumSet.h>
#include <core/str/str.h>
#include <core/type_traits.h>

namespace gfx
{
	//template <typename T>
	//constexpr ElementType
	//ElementTypeOf()
	//{
	//	using U = std::remove_cv_t<std::remove_reference_t<T>>;

	//	if constexpr (std::is_same_v<U, float>)
	//		return ElementType::kFloat;
	//	else if constexpr (std::is_same_v<U, int>)
	//		return ElementType::kInt;
	//	else if constexpr (std::is_same_v<U, uint32_t>)
	//		return ElementType::kUInt;
	//	else if constexpr (std::is_same_v<U, short>)
	//		return ElementType::kShort;
	//	else if constexpr (std::is_same_v<U, unsigned short>)
	//		return ElementType::kUShort;
	//	else if constexpr (std::is_same_v<U, bool> || std::is_same_v<U, BOOL>)
	//		return ElementType::kBool;

	//	// Vector types
	//	else if constexpr (std::is_same_v<U, glm::vec2>)
	//		return ElementType::kFloat2;
	//	else if constexpr (std::is_same_v<U, glm::vec3>)
	//		return ElementType::kFloat3;
	//	else if constexpr (std::is_same_v<U, glm::vec4>)
	//		return ElementType::kFloat4;

	//	// Matrix
	//	else if constexpr (std::is_same_v<U, glm::mat4>)
	//		return ElementType::kMat4;

	//	else
	//		return ElementType::kInvalid;
	//}
	class DynamicConstantBuffer
	{
	public:
		class Accessor
		{
		private:
			Accessor() = default;
			Accessor(uint32_t totalOffset, DynamicConstantBuffer* parent, std::string key) :
				m_totalOffset{ totalOffset }, m_parent{ parent }, m_key{ std::move(key) }
			{}

		public:
			Accessor
			At(std::string_view key) const;

			Accessor
			At(uint32_t index) const;

			Accessor
			At(std::string_view key)
			{
				return const_cast<const Accessor&>(*this).At(key);
			}

			Accessor
			At(uint32_t index)
			{
				return const_cast<const Accessor&>(*this).At(index);
			}

			template <core::type_traits::trivially_copyable T>
			operator T() const
			{
				if (IsNull())
				{
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBuffer::Accessor::operator T()",
						                "Cannot read from a null accessor" };
				}

				const auto&              entry = m_parent->GetLayoutEntry(m_key);
				core::EnumSet<GroupType> entrySet{ entry.groupType };

				if (!entrySet.Any(GroupType::Single, GroupType::Array) ||
				    entrySet.Any(GroupType::Struct))
				{
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBuffer::Accessor::operator T()",
						                "Cannot read non-leaf entry: " + m_key };
				}

				const std::byte* src = m_parent->GetRawData() + m_totalOffset;

				switch (entry.elemType)
				{
				case ElementType::kBool:
					{
						if constexpr (std::is_integral_v<T> || std::is_same_v<T, bool>)
						{
							uint32_t v{};
							std::memcpy(&v, src, sizeof(uint32_t));
							return static_cast<T>(v != 0);
						}
						break;
					}

				case ElementType::kShort:
					{
						if constexpr (std::is_integral_v<T>)
						{
							int16_t v{};
							std::memcpy(&v, src, sizeof(int16_t));
							return static_cast<T>(v);
						}
						break;
					}

				case ElementType::kUShort:
					{
						if constexpr (std::is_integral_v<T>)
						{
							uint16_t v{};
							std::memcpy(&v, src, sizeof(uint16_t));
							return static_cast<T>(v);
						}
						break;
					}

				case ElementType::kInt:
					{
						if constexpr (std::is_integral_v<T>)
						{
							int32_t v{};
							std::memcpy(&v, src, sizeof(int32_t));
							return static_cast<T>(v);
						}
						break;
					}

				case ElementType::kUInt:
					{
						if constexpr (std::is_integral_v<T>)
						{
							uint32_t v{};
							std::memcpy(&v, src, sizeof(uint32_t));
							return static_cast<T>(v);
						}
						break;
					}

				case ElementType::kFloat:
					{
						if constexpr (std::is_same_v<T, float>)
						{
							float v{};
							std::memcpy(&v, src, sizeof(float));
							return v;
						}
						break;
					}

				case ElementType::kFloat2:
					{
						if constexpr (std::is_same_v<T, glm::vec2>)
						{
							glm::vec2 v{};
							std::memcpy(&v, src, sizeof(glm::vec2));
							return v;
						}
						break;
					}

				case ElementType::kFloat3:
					{
						if constexpr (std::is_same_v<T, glm::vec3>)
						{
							glm::vec3 v{};
							std::memcpy(&v, src, sizeof(glm::vec3));
							return v;
						}
						break;
					}

				case ElementType::kFloat4:
					{
						if constexpr (std::is_same_v<T, glm::vec4>)
						{
							glm::vec4 v{};
							std::memcpy(&v, src, sizeof(glm::vec4));
							return v;
						}
						break;
					}

				case ElementType::kFloat4x4:
					{
						if constexpr (std::is_same_v<T, glm::mat4>)
						{
							glm::mat4 v{};
							std::memcpy(&v, src, sizeof(glm::mat4));
							return v;
						}
						break;
					}

				default:
					break;
				}

				throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
					                "DynamicConstantBuffer::Accessor::operator T()",
					                "Requested type does not match element type for entry: " +
					                    m_key };
			}

			template <core::type_traits::trivially_copyable T>
			Accessor&
			Assign(T val)
			{
				if (IsNull())
				{
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBuffer::Accessor::Assign",
						                "Cannot assign using a null accessor" };
				}

				const auto&              entry = m_parent->GetLayoutEntry(m_key);
				core::EnumSet<GroupType> entrySet{ entry.groupType };

				if (!entrySet.Any(GroupType::Single, GroupType::Array) ||
				    entrySet.Any(GroupType::Struct))
				{
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBuffer::Accessor::Assign",
						                "Cannot assign to non-leaf entry: " + m_key };
				}

				std::byte* dst = m_parent->GetRawData() + m_totalOffset;

				switch (entry.elemType)
				{
				case ElementType::kBool:
					{
						if constexpr (std::is_integral_v<T> || std::is_same_v<T, bool>)
						{
							uint32_t v = (val != 0) ? 1u : 0u;
							std::memcpy(dst, &v, sizeof(uint32_t));
							return *this;
						}
						break;
					}
				case ElementType::kShort:
					{
						if constexpr (std::is_integral_v<T>)
						{
							int16_t v = static_cast<int16_t>(val);
							std::memcpy(dst, &v, sizeof(int16_t));
							return *this;
						}
						break;
					}
				case ElementType::kUShort:
					{
						if constexpr (std::is_integral_v<T>)
						{
							uint16_t v = static_cast<uint16_t>(val);
							std::memcpy(dst, &v, sizeof(uint16_t));
							return *this;
						}
						break;
					}
				case ElementType::kInt:
					{
						if constexpr (std::is_integral_v<T> || std::is_same_v<T, bool>)
						{
							uint32_t v = static_cast<uint32_t>(val);
							std::memcpy(dst, &v, sizeof(uint32_t));
							return *this;
						}
						break;
					}
				case ElementType::kUInt:
					{
						if constexpr (std::is_integral_v<T>)
						{
							uint32_t v = static_cast<uint32_t>(val);
							std::memcpy(dst, &v, sizeof(uint32_t));
							return *this;
						}
						break;
					}
				case ElementType::kFloat:
					{
						if constexpr (std::is_same_v<T, float>)
						{
							std::memcpy(dst, &val, sizeof(float));
							return *this;
						}
						break;
					}
				case ElementType::kFloat2:
					{
						if constexpr (std::is_same_v<T, glm::vec2>)
						{
							std::memcpy(dst, &val, sizeof(glm::vec2));
							return *this;
						}
						break;
					}
				case ElementType::kFloat3:
					{
						if constexpr (std::is_same_v<T, glm::vec3>)
						{
							std::memcpy(dst, &val, sizeof(glm::vec3));
							return *this;
						}
						break;
					}
				case ElementType::kFloat4:
					{
						if constexpr (std::is_same_v<T, glm::vec4>)
						{
							std::memcpy(dst, &val, sizeof(glm::vec4));
							return *this;
						}
						break;
					}
				case ElementType::kFloat4x4:
					{
						if constexpr (std::is_same_v<T, glm::mat4>)
						{
							std::memcpy(dst, &val, sizeof(glm::mat4));
							return *this;
						}
						break;
					}

				default:
					break;
				}

				throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
					                "DynamicConstantBuffer::Accessor::Assign",
					                "Assigned type does not match element type for entry: " +
					                    m_key };
			}

			template <core::type_traits::trivially_copyable T>
			void
			operator=(T val) noexcept
			{
				try
				{
					Assign<T>(val);
				}
				catch (...)
				{}
			}

			[[nodiscard]] bool
			IsNull() const noexcept
			{
				return m_key == "" || m_parent == nullptr;
			}

			[[nodiscard]] bool
			IsValid() const noexcept
			{
				return !IsNull();
			}

			[[nodiscard]] uint32_t
			Size() const noexcept
			{
				if (IsNull())
					return 0;
				auto& entry = m_parent->GetLayoutEntry(m_key);

				return entry.elemSize * entry.count;
			}

			[[nodiscard]] bool
			IsArray() const noexcept
			{
				if (IsNull())
					return false;
				auto& entry = m_parent->GetLayoutEntry(m_key);
				return entry.stride != 0;
			}

			Accessor
			operator[](std::string_view name) noexcept;

			Accessor
			operator[](uint32_t idx) noexcept;

		private:
			uint32_t               m_totalOffset = 0;
			DynamicConstantBuffer* m_parent      = nullptr;
			std::string            m_key         = "";

			friend class DynamicConstantBuffer;
		};

		class View
		{
		public:
			View(View&& rhs) noexcept : m_ptr(rhs.m_ptr) { rhs.m_ptr = nullptr; }

			View(const View& rhs) noexcept : m_ptr(rhs.m_ptr) {}

			View() = default;

			[[nodiscard]] bool
			IsNull() const noexcept
			{
				return m_ptr == nullptr;
			}
			[[nodiscard]] bool
			IsValid() const noexcept
			{
				return m_ptr != nullptr;
			}

			const Accessor
			At(std::string_view name) const
			{
				return m_ptr->At(name);
			}

			const Accessor
			operator[](std::string_view name) const noexcept
			{
				return m_ptr->operator[](name);
			}

			View&
			operator=(const View& rhs)
			{
				if (this != &rhs)
				{
					m_ptr = rhs.m_ptr;
				}
				return *this;
			}

			View&
			operator=(View&& rhs) noexcept
			{
				if (this != &rhs)
				{
					m_ptr     = rhs.m_ptr;
					rhs.m_ptr = nullptr;
				}
				return *this;
			}

		private:
			View(DynamicConstantBuffer const* ptr) noexcept : m_ptr(ptr) {}

			DynamicConstantBuffer const* m_ptr = nullptr;

			friend class DynamicConstantBuffer;
		};

	public:
		DynamicConstantBuffer() noexcept                    = default;
		DynamicConstantBuffer(const DynamicConstantBuffer&) = delete;
		DynamicConstantBuffer(DynamicConstantBuffer&&) noexcept;

		DynamicConstantBuffer(
			nvrhi::DeviceHandle              device,
			const DynamicConstantBufferDesc& elementDesc);

		DynamicConstantBuffer(
			nvrhi::DeviceHandle device,
			std::string_view    shaderPath,
			uint32_t            bindingSlot,
			uint32_t            bindingSpace,
			bool                isVolatile = false);

		DynamicConstantBuffer&
		operator=(const DynamicConstantBuffer&) = delete;

		DynamicConstantBuffer&
		operator=(DynamicConstantBuffer&&) noexcept;

		Accessor
		operator[](std::string_view name) noexcept;

		const Accessor
		operator[](std::string_view name) const noexcept
		{
			return const_cast<DynamicConstantBuffer*>(this)->At(name);
		}

		View
		GetView() const noexcept
		{
			return View{ this };
		}

		const Accessor
		At(std::string_view name) const;

		Accessor
		At(std::string_view name)
		{
			return const_cast<const DynamicConstantBuffer&>(*this).At(name);
		}

		nvrhi::BindingLayoutItem
		GetBindingLayoutItem(uint32_t slot) const noexcept;

		nvrhi::BindingSetItem
		GetBindingSetItem(uint32_t slot) const noexcept;

		const LayoutEntry&
		GetLayoutEntry(std::string_view name) const;

		void
		Update(nvrhi::CommandListHandle cmdList) noexcept;

		[[nodiscard]]
		std::string_view
		GetName() const noexcept
		{
			return m_bufferDesc.debugName;
		}

		[[nodiscard]] operator nvrhi::BufferHandle() const noexcept { return m_buf; }
		[[nodiscard]] operator nvrhi::IBuffer*() const noexcept { return m_buf.Get(); }

		[[nodiscard]]
		bool
		Initialized() const noexcept;

		void
		Release() noexcept;

		[[nodiscard]] [[deprecated("Unsafe: only use for testing or inspection purposes")]]
		std::byte*
		GetRawData() noexcept
		{
			return m_data.get();
		}

		const nvrhi::BufferHandle
		GetBufferHandle() const noexcept
		{
			return m_buf;
		}

		[[nodiscard]]
		uint32_t
		GetTotalSize() const noexcept
		{
			return m_bufferDesc.byteSize;
		}

	private:
		void
		Init(nvrhi::DeviceHandle device, const DynamicConstantBufferDesc& elementDesc);

		[[nodiscard]]
		bool
		IsVolatile() const noexcept
		{
			return m_bufferDesc.isVolatile;
		}

		LayoutMap                    m_layoutMap;
		nvrhi::BufferDesc            m_bufferDesc;
		nvrhi::BufferHandle          m_buf;
		std::unique_ptr<std::byte[]> m_data;

		friend class Accessor;
	};
}
