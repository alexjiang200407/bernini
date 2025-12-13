#include "buffer/DynamicConstantBuffer.h"

namespace gfx
{
	DynamicConstantBuffer::DynamicConstantBuffer(
		nvrhi::DeviceHandle device,
		DynamicBufferDesc   elementDesc) : DynamicBuffer{ elementDesc, 1 }
	{
		auto bufferDesc = nvrhi::BufferDesc{};
		bufferDesc.setByteSize(static_cast<uint32_t>(elementDesc.GetTotalSize()))
			.setIsConstantBuffer(true)
			.setInitialState(nvrhi::ResourceStates::ConstantBuffer)
			.setKeepInitialState(false)
			.setDebugName(std::string{ GetName() });

		if (elementDesc.updateFrequency == DynamicBufferDesc::UpdateFrequency::kPerFrame)
		{
			static constexpr auto maxVersions = 16u;
			bufferDesc.setIsVolatile(true);
			bufferDesc.setMaxVersions(maxVersions);
		}

		m_buf = device->createBuffer(bufferDesc);
	}

	DynamicBufferItem::View
	DynamicConstantBuffer::operator[](std::string_view name)
	{
		return DynamicBuffer::operator[](0)[name];
	}

	nvrhi::BindingLayoutItem
	DynamicConstantBuffer::GetBindingLayoutItem(uint32_t slot) const noexcept
	{
		if (GetDesc().updateFrequency == DynamicBufferDesc::UpdateFrequency::kPerDraw)
		{
			return nvrhi::BindingLayoutItem::VolatileConstantBuffer(slot);
		}
		return nvrhi::BindingLayoutItem::ConstantBuffer(slot);
	}

	nvrhi::BindingSetItem
	DynamicConstantBuffer::GetBindingSetItem(uint32_t slot) const noexcept
	{
		return nvrhi::BindingSetItem::ConstantBuffer(slot, m_buf);
	}
}
