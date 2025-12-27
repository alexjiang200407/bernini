#pragma once
#include "ElementType.h"
#include "GfxException.h"
#include "buffer/DynamicBuffer.h"
#include "buffer/DynamicConstantBufferDesc.h"
#include <core/str/str.h>
#include <core/type_traits.h>

namespace gfx
{
	class DynamicConstantBuffer : public DynamicBuffer
	{
	public:
		class View
		{
		private:
			View() = default;
			View(uint32_t totalOffset, DynamicConstantBuffer* parent, std::string key) :
				m_totalOffset{ totalOffset }, m_parent{ parent }, m_key{ std::move(key) }
			{}

		public:
			View
			At(std::string_view key) const;

			View
			At(uint32_t index) const;

			View
			At(std::string_view key)
			{
				return const_cast<const View&>(*this).At(key);
			}

			View
			At(uint32_t index)
			{
				return const_cast<const View&>(*this).At(index);
			}

			template <core::type_traits::trivially_copyable T>
			View&
			Assign(T val)
			{
				if (IsNull())
					throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
						                "DynamicConstantBuffer::View::Assign",
						                "Cannot assign using a null view" };

				auto& entry = m_parent->GetLayoutEntry(m_key);
				if (sizeof(T) != entry.elemSize)
				{
					throw GfxException{
						GFX_RESULT_DYNAMIC_BUFFER,
						"DynamicConstantBuffer::View::Assign",
						"Size of assigned value does not match element size for entry: " + m_key
					};
				}
				std::memcpy(m_parent->GetRawData() + m_totalOffset, &val, sizeof(T));

				return *this;
			}

			/// <summary>
			/// Assignment but swallow any exceptions
			/// </summary>
			/// <typeparam name="T">Any type that is trivially copyable</typeparam>
			/// <param name="val">value</param>
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
				return entry.IsArray();
			}

			View
			operator[](std::string_view name) noexcept;

			View
			operator[](uint32_t idx) noexcept;

		private:
			uint32_t               m_totalOffset = 0;
			DynamicConstantBuffer* m_parent      = nullptr;
			std::string            m_key         = "";

			friend class DynamicConstantBuffer;
		};

	public:
		DynamicConstantBuffer() noexcept = default;
		DynamicConstantBuffer(
			nvrhi::DeviceHandle              device,
			const DynamicConstantBufferDesc& elementDesc);

		DynamicConstantBuffer(
			nvrhi::DeviceHandle device,
			std::string_view    shaderPath,
			uint32_t            bindingSlot,
			uint32_t            bindingSpace,
			bool                isVolatile = false);

		View
		operator[](std::string_view name) noexcept;

		const View
		operator[](std::string_view name) const noexcept
		{
			const_cast<const DynamicConstantBuffer&>(*this).At(name);
		}

		const View
		At(std::string_view name) const;

		View
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

		DynamicConstantBuffer&
		operator=(DynamicConstantBuffer&&) noexcept = default;

		DynamicConstantBuffer(DynamicConstantBuffer&&) noexcept = default;

	protected:
		void
		Init(nvrhi::DeviceHandle device, const DynamicConstantBufferDesc& elementDesc);

	private:
		LayoutMap m_layoutMap;

		friend class View;
	};
}
