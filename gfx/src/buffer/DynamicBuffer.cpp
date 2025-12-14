#include "buffer/DynamicBuffer.h"

namespace gfx
{
	uint32_t
	sizeOfElementType(ElementType type)
	{
		switch (type)
		{
		case ElementType::kFloat:
			return 4;
		case ElementType::kFloat2:
			return 8;
		case ElementType::kFloat3:
			return 12;
		case ElementType::kFloat4:
			return 16;
		case ElementType::kFloat4x4:
			return 64;
		}

		return 0;
	}

	nvrhi::Format
	elementTypeToNvrhiFormat(ElementType type)
	{
		switch (type)
		{
		case ElementType::kFloat:
			return nvrhi::Format::R32_FLOAT;
		case ElementType::kFloat2:
			return nvrhi::Format::RG32_FLOAT;
		case ElementType::kFloat3:
			return nvrhi::Format::RGB32_FLOAT;
		case ElementType::kFloat4:
			return nvrhi::Format::RGBA32_FLOAT;
		case ElementType::kFloat4x4:
			return nvrhi::Format::RGBA32_FLOAT;
		}

		return nvrhi::Format::UNKNOWN;
	}

	DynamicBufferItem::DynamicBufferItem(void* baseData, const DynamicBufferDesc& desc)
	{
		m_data          = baseData;
		uint32_t offset = 0;
		for (auto& [name, type] : desc.elements)
		{
			auto entry         = EntryDesc{};
			entry.offset       = offset;
			entry.type         = type;
			m_elementMap[name] = entry;
			offset += sizeOfElementType(entry.type);
		}
		m_totalSize = offset;
	}

	DynamicBufferItem::View
	DynamicBufferItem::operator[](std::string_view name) noexcept
	{
		auto it = m_elementMap.find(std::string{ name });
		if (it == m_elementMap.end())
			return {};
		auto& desc  = it->second;
		auto  view  = View{};
		view.m_data = reinterpret_cast<uint8_t*>(m_data) + desc.offset;
		view.m_type = desc.type;
		return view;
	}

	DynamicBufferItem::View
	DynamicBufferItem::At(std::string_view name)
	{
		auto it = m_elementMap.find(std::string{ name });
		if (it == m_elementMap.end())
			throw core::except::BerniniException{ "DynamicBuffer exception",
				                                  "Element not found in DynamicBufferItem::At" };
		auto& desc  = it->second;
		auto  view  = View{};
		view.m_data = reinterpret_cast<uint8_t*>(m_data) + desc.offset;
		view.m_type = desc.type;
		return view;
	}

	DynamicBuffer::DynamicBuffer(const DynamicBufferDesc& elementDesc, uint32_t count) :
		m_desc{ elementDesc }, m_count(count)
	{
		m_totalSize = m_desc.GetTotalSize() * count;
		m_data      = std::malloc(m_totalSize);
		if (!m_data)
			throw std::bad_alloc();
	}

	DynamicBuffer::DynamicBuffer(DynamicBuffer&& other)
	{
		m_buf             = std::move(other.m_buf);
		m_desc            = other.m_desc;
		m_count           = other.m_count;
		m_data            = other.m_data;
		m_totalSize       = other.m_totalSize;
		other.m_count     = 0u;
		other.m_data      = nullptr;
		other.m_totalSize = 0u;
	}

	DynamicBuffer::~DynamicBuffer() noexcept { Release(); }

	bool
	DynamicBufferItem::View::operator==(const View& rhs) const noexcept
	{
		if (!Valid() || !rhs.Valid())
			return false;

		if (m_type != rhs.m_type)
			return false;

		return std::memcmp(m_data, rhs.m_data, sizeOfElementType(m_type)) == 0;
	}

	DynamicBufferItem
	DynamicBuffer::operator[](uint32_t index)
	{
		if (index >= m_count)
			throw core::except::BerniniException{ "DynamicBuffer exception", "Index out of range" };
		void* elementData = reinterpret_cast<uint8_t*>(m_data) + index * m_desc.GetTotalSize();
		return DynamicBufferItem{ elementData, m_desc };
	}

	uint32_t
	DynamicBuffer::GetCount() const noexcept
	{
		return m_count;
	}

	const DynamicBufferDesc&
	DynamicBuffer::GetDesc() const noexcept
	{
		return m_desc;
	}

	DynamicBufferDesc&
	DynamicBufferDesc::AddElement(std::string_view name, ElementType type)
	{
		elements.emplace_back(std::string{ name }, type);
		return *this;
	}

	DynamicBufferDesc&
	gfx::DynamicBufferDesc::SetUpdateFrequency(UpdateFrequency freq)
	{
		this->updateFrequency = freq;
		return *this;
	}

	uint32_t
	DynamicBufferDesc::GetTotalSize() const noexcept
	{
		uint32_t total = 0;
		for (const auto& e : elements) total += sizeOfElementType(e.type);
		return total;
	}

	DynamicBufferDesc&
	DynamicBufferDesc::SetName(std::string_view name)
	{
		this->name = std::string(name);
		return *this;
	}

	DynamicBuffer&
	DynamicBuffer::operator=(DynamicBuffer&& other) noexcept
	{
		if (this != std::addressof(other))
		{
			Release();
			m_buf             = std::move(other.m_buf);
			m_desc            = other.m_desc;
			m_count           = other.m_count;
			m_data            = other.m_data;
			m_totalSize       = other.m_totalSize;
			other.m_count     = 0u;
			other.m_data      = nullptr;
			other.m_totalSize = 0u;
		}
		return *this;
	}

	void
	DynamicBuffer::Update(nvrhi::CommandListHandle cmdList) noexcept
	{
		if (m_buf)
		{
			cmdList->writeBuffer(m_buf, m_data, m_totalSize);
		}
		else
		{
			std::call_once(m_warnedAboutNoBuffer, [this]() {
				logger::warn(
					"DynamicBuffer::Update called on '{}' but no buffer has been created",
					GetName());
			});
		}
	}

	std::string_view
	DynamicBuffer::GetName() const noexcept
	{
		return m_desc.name.empty() ? std::string_view{ "Unnamed Buffer" } :
		                             std::string_view{ m_desc.name };
	}

	bool
	DynamicBuffer::Initialized() const noexcept
	{
		return m_buf != nullptr;
	}

	void
	DynamicBuffer::Reset() noexcept
	{
		if (m_data)
		{
			std::free(m_data);
			m_data = nullptr;
		}

		m_count     = 0;
		m_totalSize = 0;
	}

	void
	DynamicBuffer::Release() noexcept
	{
		Reset();
		m_buf.Reset();
	}

	DynamicBufferItem::View::
	operator bool() const noexcept
	{
		return Valid();
	}

	bool
	DynamicBufferItem::View::Valid() const noexcept
	{
		return m_data != nullptr && m_type != ElementType::kInvalid;
	}

	std::ostream&
	operator<<(std::ostream& os, const DynamicBufferItem::View& view) noexcept
	{
		switch (view.m_type)
		{
		case ElementType::kFloat:
			{
				const auto& val = view.As<float>();
				os << val;
				break;
			}
		case ElementType::kFloat2:
			{
				const auto& val = view.As<glm::vec2>();
				os << "(" << val.x << ", " << val.y << ")";
				break;
			}
		case ElementType::kFloat3:
			{
				const auto& val = view.As<glm::vec3>();
				os << "(" << val.x << ", " << val.y << ", " << val.z << ")";
				break;
			}
		case ElementType::kFloat4:
			{
				const auto& val = view.As<glm::vec4>();
				os << "(" << val.x << ", " << val.y << ", " << val.z << ", " << val.w << ")";
				break;
			}
		case ElementType::kFloat4x4:
			{
				const auto& mat = view.As<glm::mat4>();
				os << "\n[";
				for (int row = 0; row < 4; ++row)
				{
					if (row > 0)
						os << " ";
					os << "(";
					for (int col = 0; col < 4; ++col)
					{
						if (col > 0)
							os << ", ";
						os << std::setw(6) << mat[col][row];
					}
					os << ")";
					if (row < 3)
						os << "\n";
				}
				os << "]";
				break;
			}
		default:
			os << "<Invalid DynamicBufferItem::View>";
			break;
		}

		return os;
	}
}
