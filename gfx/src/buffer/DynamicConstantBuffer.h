#pragma once
#include "buffer/DynamicBuffer.h"

namespace gfx
{
	class DynamicConstantBuffer : public DynamicBuffer
	{
	public:
		DynamicConstantBuffer() noexcept = default;
		DynamicConstantBuffer(nvrhi::DeviceHandle device, const DynamicBufferDesc& elementDesc);

		DynamicBufferItem::View
		operator[](std::string_view name);

		nvrhi::BindingLayoutItem
		GetBindingLayoutItem(uint32_t slot) const noexcept;

		nvrhi::BindingSetItem
		GetBindingSetItem(uint32_t slot) const noexcept;

	private:
		using DynamicBuffer::operator[];
	};
}
