#pragma once
#include <core/except/BerniniException.h>
#include <core/type_traits.h>

namespace gfx
{
	enum class ElementType
	{
		kInvalid = -1,
		kFloat,
		kFloat2,
		kFloat3,
		kFloat4,
		kFloat4x4,
	};

	uint32_t
	sizeOfElementType(ElementType format);

	nvrhi::Format
	elementTypeToNvrhiFormat(ElementType type);

	struct DynamicBufferDescElement
	{
		std::string name;
		ElementType type;

		uint32_t
		Size() const noexcept
		{
			return sizeOfElementType(type);
		}
	};

	struct DynamicBufferDesc
	{
		enum class UpdateFrequency
		{
			kPerFrame,
			kPerDraw,
		};

		UpdateFrequency                       updateFrequency{ UpdateFrequency::kPerFrame };
		std::vector<DynamicBufferDescElement> elements{};
		std::string                           name{};

		DynamicBufferDesc&
		AddElement(std::string_view name, ElementType format);

		DynamicBufferDesc&
		SetUpdateFrequency(UpdateFrequency freq);

		uint32_t
		GetTotalSize() const noexcept;

		DynamicBufferDesc&
		SetName(std::string_view name);
	};

	class DynamicBufferItem
	{
	private:
		struct EntryDesc
		{
			uint32_t    offset;
			ElementType type;
		};

	public:
		class View
		{
			friend class DynamicBufferItem;
			friend std::ostream&
			operator<<(std::ostream& os, const View& view) noexcept;

		private:
			View() = default;

		public:
			template <core::type_traits::trivially_copyable T>
			[[nodiscard]] bool
			operator==(const T& val) const noexcept
			{
				if (!Valid())
					return false;

				if (sizeof(T) != sizeOfElementType(m_type))
					return false;

				return std::memcmp(m_data, &val, sizeof(T)) == 0;
			}

			[[nodiscard]] bool
			operator==(const View& rhs) const noexcept;

			explicit
			operator bool() const noexcept;

			[[nodiscard]]
			bool
			Valid() const noexcept;

			template <core::type_traits::trivially_copyable T>
			void
			operator=(const T& val) noexcept
			{
				if (!Valid())
					return;

				if (sizeof(T) != sizeOfElementType(m_type))
					return;

				auto& ref = *static_cast<T*>(m_data);
				std::memcpy(m_data, &val, sizeof(T));
			}

			template <core::type_traits::trivially_copyable T>
			T&
			Assign(const T& val)
			{
				if (m_data == nullptr || m_type == ElementType::kInvalid)
					throw core::except::BerniniException{ "DynamicBuffer exception",
						                                  "Empty View used for View::Assign" };

				if (sizeof(T) != sizeOfElementType(m_type))
					throw core::except::BerniniException{ "DynamicBuffer exception",
						                                  "Size mismatch in View::Assign" };

				auto& ref = *static_cast<T*>(m_data);
				std::memcpy(m_data, &val, sizeof(T));
				return ref;
			}

		private:
			template <core::type_traits::trivially_copyable T>
			T&
			As() const noexcept
			{
				return *static_cast<T*>(m_data);
			}

		private:
			void*       m_data = nullptr;
			ElementType m_type = ElementType::kInvalid;
		};

	public:
		DynamicBufferItem(void* baseData, const DynamicBufferDesc& desc);

		View
		operator[](std::string_view name) noexcept;

		View
		At(std::string_view name);

		template <typename T>
		void
		Set(std::string_view name, const T& value)
		{
			auto it = m_elementMap.find(name);
			if (it == m_elementMap.end())
				throw core::except::BerniniException{
					"DynamicBuffer exception",
					"Element not found in DynamicBufferItem::Set"
				};
			EntryDesc& desc = it->second;
			std::memcpy(reinterpret_cast<uint8_t*>(m_data) + desc.offset, &value, sizeof(T));
		}

		template <typename T>
		[[nodiscard]]
		T
		Get(std::string_view name) const
		{
			auto it = m_elementMap.find(std::string{ name });
			if (it == m_elementMap.end())
				throw core::except::BerniniException{
					"DynamicBuffer exception",
					"Element not found in DynamicBufferItem::Get"
				};
			auto& desc  = const_cast<EntryDesc&>(it->second);
			auto  value = T{};
			std::memcpy(&value, reinterpret_cast<uint8_t*>(m_data) + desc.offset, sizeof(T));
			return value;
		}

		std::unordered_map<std::string, EntryDesc> m_elementMap;
		void*                                      m_data;
		uint32_t                                   m_totalSize;
	};

	class DynamicBuffer
	{
	public:
		DynamicBuffer() noexcept = default;
		DynamicBuffer(const DynamicBufferDesc& elementDesc, uint32_t count);
		DynamicBuffer(DynamicBuffer&& other);

		~DynamicBuffer() noexcept;

		DynamicBuffer&
		operator=(DynamicBuffer&& other) noexcept;

		[[nodiscard]]
		DynamicBufferItem
		operator[](uint32_t index);

		[[nodiscard]]
		uint32_t
		GetCount() const noexcept;

		[[nodiscard]]
		const DynamicBufferDesc&
		GetDesc() const noexcept;

		void
		Update(nvrhi::CommandListHandle cmdList) noexcept;

		[[nodiscard]]
		std::string_view
		GetName() const noexcept;

		[[nodiscard]] operator nvrhi::BufferHandle() const noexcept { return m_buf; }

		[[nodiscard]] operator nvrhi::IBuffer*() const noexcept { return m_buf.Get(); }

		[[nodiscard]]
		bool
		Initialized() const noexcept;

		void
		Reset() noexcept;

		void
		Release() noexcept;

	protected:
		nvrhi::BufferHandle m_buf;

	private:
		DynamicBufferDesc m_desc{};
		std::once_flag    m_warnedAboutNoBuffer{};
		uint32_t          m_count     = 0u;
		void*             m_data      = nullptr;
		uint32_t          m_totalSize = 0u;
	};

	std::ostream&
	operator<<(std::ostream& os, const DynamicBufferItem::View& view) noexcept;
}
