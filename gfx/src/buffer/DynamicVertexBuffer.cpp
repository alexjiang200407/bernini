#include "buffer/DynamicVertexBuffer.h"
#include "shader_reflect/ShaderInput.h"

namespace gfx
{
	DynamicBufferItem::DynamicBufferItem(
		void*                                     baseData,
		std::span<const DynamicBufferDescElement> elements)
	{
		m_data          = baseData;
		uint32_t offset = 0;
		for (auto& elem : elements)
		{
			offset += elem.GetOffset();

			auto entry              = EntryDesc{};
			entry.offset            = offset;
			entry.type              = elem.type;
			m_elementMap[elem.name] = entry;

			offset += elem.Size();
		}

		m_totalSize = offset;
	}

	DynamicBufferItem::Accessor
	DynamicBufferItem::operator[](std::string_view name) noexcept
	{
		auto it = m_elementMap.find(std::string{ name });
		if (it == m_elementMap.end())
			return {};
		auto& desc  = it->second;
		auto  view  = Accessor{};
		view.m_data = reinterpret_cast<uint8_t*>(m_data) + desc.offset;
		view.m_type = desc.type;
		return view;
	}

	DynamicBufferItem::Accessor
	DynamicBufferItem::At(std::string_view name)
	{
		auto it = m_elementMap.find(std::string{ name });
		if (it == m_elementMap.end())
			throw core::except::BerniniException{ "DynamicBuffer exception",
				                                  "Element not found in DynamicBufferItem::At" };
		auto& desc  = it->second;
		auto  view  = Accessor{};
		view.m_data = reinterpret_cast<uint8_t*>(m_data) + desc.offset;
		view.m_type = desc.type;
		return view;
	}

	DynamicVertexBuffer::DynamicVertexBuffer(DynamicVertexBuffer&& other) noexcept
	{
		m_count    = std::exchange(other.m_count, 0u);
		m_elements = std::exchange(other.m_elements, {});
	}

	DynamicVertexBuffer::~DynamicVertexBuffer() noexcept { Release(); }

	bool
	DynamicBufferItem::Accessor::operator==(const Accessor& rhs) const noexcept
	{
		if (!Valid() || !rhs.Valid())
			return false;

		if (m_type != rhs.m_type)
			return false;

		return std::memcmp(m_data, rhs.m_data, sizeOfElementType(m_type)) == 0;
	}

	DynamicBufferItem
	DynamicVertexBuffer::operator[](uint32_t index)
	{
		if (index >= m_count)
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicBuffer exception",
				                "Index out of range" };

		auto  stride      = GetTotalSize() / m_count;
		void* elementData = GetRawData() + index * stride;
		return DynamicBufferItem{ elementData, m_elements };
	}

	uint32_t
	DynamicVertexBuffer::GetCount() const noexcept
	{
		return m_count;
	}

	DynamicBufferItem::Accessor::operator bool() const noexcept { return Valid(); }

	DynamicBufferDesc&
	DynamicBufferDesc::AddElement(std::string_view name, ElementType type, uint32_t offset)
	{
		if (std::find_if(
				elements.begin(),
				elements.end(),
				[name](const DynamicBufferDescElement& val) { return val.name == name; }) !=
		    elements.end())
		{
			throw GfxException{ GFX_RESULT_DYNAMIC_BUFFER,
				                "DynamicBufferDesc exception",
				                "Element with the same name already exists" };
		}

		elements.emplace_back(std::string{ name }, type, offset);
		return *this;
	}

	DynamicBufferDesc&
	gfx::DynamicBufferDesc::SetIsVolatile(bool isVolatile) noexcept
	{
		this->isVolatile = isVolatile;
		return *this;
	}

	uint32_t
	DynamicBufferDesc::GetTotalSize() const noexcept
	{
		uint32_t total = 0;
		for (const auto& e : elements) total += e.TotalSize();
		return total;
	}

	DynamicBufferDesc&
	DynamicBufferDesc::SetName(std::string_view name)
	{
		this->name = std::string(name);
		return *this;
	}

	DynamicVertexBuffer&
	DynamicVertexBuffer::operator=(DynamicVertexBuffer&& other) noexcept
	{
		if (this != std::addressof(other))
		{
			m_count    = std::exchange(other.m_count, 0u);
			m_elements = std::exchange(other.m_elements, {});
			DynamicBuffer::operator=(std::move(other));
		}
		return *this;
	}

	bool
	DynamicBufferItem::Accessor::Valid() const noexcept
	{
		return m_data != nullptr && m_type != ElementType::kInvalid;
	}

	std::ostream&
	operator<<(std::ostream& os, const DynamicBufferItem::Accessor& view) noexcept
	{
		switch (view.m_type)
		{
		case ElementType::kEmpty:
			{
				os << "<Empty>";
				break;
			}
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

		case ElementType::kInt:
			{
				const auto& val = view.As<int32_t>();
				os << val;
				break;
			}

		case ElementType::kUInt:
			{
				const auto& val = view.As<uint32_t>();
				os << val;
				break;
			}

		case ElementType::kShort:
			{
				const auto& val = view.As<int16_t>();
				os << val;
				break;
			}
		case ElementType::kUShort:
			{
				const auto& val = view.As<uint16_t>();
				os << val;
				break;
			}

		default:
			os << "<Invalid DynamicBufferItem::Accessor>";
			break;
		}

		return os;
	}

	uint32_t
	DynamicBufferDescElement::Size() const noexcept
	{
		return sizeOfElementType(type);
	}

	uint32_t
	DynamicBufferDescElement::GetOffset() const noexcept
	{
		return offset;
	}

	DynamicVertexBuffer::DynamicVertexBuffer(
		nvrhi::DeviceHandle device,
		DynamicBufferDesc&& elementDesc,
		uint32_t            count)
	{
		auto desc = nvrhi::BufferDesc{};
		desc.setByteSize(elementDesc.GetTotalSize() * count)
			.setIsVertexBuffer(true)
			.setKeepInitialState(true)
			.setInitialState(nvrhi::ResourceStates::VertexBuffer)
			.setDebugName(elementDesc.name);

		if (elementDesc.IsVolatile())
		{
			desc.setIsVolatile(true).setMaxVersions(16);
		}

		DynamicBuffer::Init(device, desc);

		m_elements = std::move(elementDesc.elements);
		m_count    = count;
	}

	nvrhi::InputLayoutHandle
	DynamicVertexBuffer::GenerateVertexLayout(
		nvrhi::DeviceHandle device,
		nvrhi::ShaderHandle vertexShader) const
	{
		auto input = getVertexAttributes(vertexShader);

		std::vector<nvrhi::VertexAttributeDesc> vertexAttrs;
		vertexAttrs.reserve(input.size());

		uint32_t bufIdx = 0;

		for (const auto& attr : input)
		{
			uint32_t offset = 0;
			bool     found  = false;

			auto stride = GetTotalSize() / m_count;

			for (const auto& elem : m_elements)
			{
				if (semanticMatches(elem.GetName(), attr.semanticName, attr.semanticIndex))
				{
					// Not same format exit.
					//if (attr.format != elementTypeToNvrhiFormat(elem.GetType()))
					//{
					//	break;
					//}

					vertexAttrs.emplace_back(nvrhi::VertexAttributeDesc{}
					                             .setName(attr.semanticName)
					                             .setFormat(attr.format)
					                             .setBufferIndex(bufIdx)
					                             .setOffset(offset)
					                             .setElementStride(stride)
					                             .setIsInstanced(false));

					found = true;
					break;
				}

				offset += elem.Size();
			}

			if (!found)
			{
				logger::warn(
					"Vertex input '{}{}' not found in vertex buffer '{}'",
					attr.semanticName,
					attr.semanticIndex,
					GetName());

				return {};
			}

			++bufIdx;
		}

		return device->createInputLayout(
			vertexAttrs.data(),
			uint32_t(vertexAttrs.size()),
			vertexShader);
	}
}
