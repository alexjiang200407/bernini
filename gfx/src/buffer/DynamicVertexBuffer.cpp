#include "buffer/DynamicVertexBuffer.h"

namespace gfx
{
	DynamicVertexBuffer::DynamicVertexBuffer(
		nvrhi::DeviceHandle      device,
		const DynamicBufferDesc& elementDesc,
		uint32_t                 count) : DynamicBuffer{ elementDesc, count }
	{
		auto bufferDesc = nvrhi::BufferDesc{};
		auto name       = std::string{ GetName() };
		bufferDesc.setByteSize(static_cast<uint64_t>(elementDesc.GetTotalSize()) * count)
			.setIsVertexBuffer(true)
			.setInitialState(nvrhi::ResourceStates::VertexBuffer)
			.setKeepInitialState(false)
			.setDebugName(name);

		m_buf = device->createBuffer(bufferDesc);
	}

	nvrhi::InputLayoutHandle
	DynamicVertexBuffer::GenerateVertexLayout(
		nvrhi::DeviceHandle device,
		nvrhi::ShaderHandle vertexShader) const
	{
		auto        input    = getVertexAttributes(vertexShader);
		const auto& elemDesc = GetDesc();

		std::vector<nvrhi::VertexAttributeDesc> vertexAttrs;
		vertexAttrs.reserve(input.size());

		uint32_t bufIdx = 0;

		for (const auto& attr : input)
		{
			uint32_t offset = 0;
			bool     found  = false;

			for (const auto& elem : elemDesc.GetElements())
			{
				if (semanticMatches(elem.GetName(), attr.semanticName, attr.semanticIndex))
				{
					// Not same format exit.
					if (attr.format != elementTypeToNvrhiFormat(elem.GetType()))
					{
						break;
					}

					vertexAttrs.emplace_back(
						nvrhi::VertexAttributeDesc{}
							.setName(attr.semanticName)
							.setFormat(attr.format)
							.setBufferIndex(bufIdx)
							.setOffset(offset)
							.setElementStride(elemDesc.GetTotalSize())
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
