#pragma once
#include "GfxException.h"
#include "frame_graph/FrameGraphView.h"
#include <core/type_traits.h>

namespace gfx
{
	struct GPULocalBufferDesc
	{
		uint32_t startingLen = 1;

		nvrhi::BufferDesc bufferDesc;

		GPULocalBufferDesc&
		SetName(const std::string& val)
		{
			bufferDesc.setDebugName(val);
			return *this;
		}

		GPULocalBufferDesc&
		SetStartingLen(uint32_t val)
		{
			startingLen = val;
			return *this;
		}

		GPULocalBufferDesc&
		SetIsDrawIndirect(bool isDrawIndirect = true)
		{
			bufferDesc.setIsDrawIndirectArgs(isDrawIndirect);
			return *this;
		}

		GPULocalBufferDesc&
		SetKeepInitialState(bool keepInitial = true)
		{
			bufferDesc.setKeepInitialState(keepInitial);
			return *this;
		}

		GPULocalBufferDesc&
		SetInitialState(nvrhi::ResourceStates state)
		{
			bufferDesc.setInitialState(state);
			return *this;
		}
	};

	template <core::type_traits::trivially_copyable T>
	class GPULocalBuffer
	{
	public:
		using View = FrameGraphView<GPULocalBuffer<T>>;

	public:
		GPULocalBuffer() noexcept             = default;
		GPULocalBuffer(const GPULocalBuffer&) = delete;

		GPULocalBuffer(nvrhi::DeviceHandle device, const GPULocalBufferDesc& desc)
		{
			Init(device, desc);
		}

		void
		Init(nvrhi::DeviceHandle device, const GPULocalBufferDesc& desc)
		{
			auto bufferDesc = desc.bufferDesc;
			bufferDesc.setStructStride(sizeof(T)).setCanHaveUAVs(true).setByteSize(
				sizeof(T) * desc.startingLen);

			m_buffer = device->createBuffer(bufferDesc);
		}

		void
		Resize(nvrhi::DeviceHandle device, uint32_t newLen)
		{
			auto bufferDesc = m_buffer->getDesc();
			bufferDesc.setByteSize(sizeof(T) * newLen);
			m_buffer = device->createBuffer(bufferDesc);
		}

		[[nodiscard]] nvrhi::BindingLayoutItem
		GetBindingLayoutItemUAV(uint32_t slot) const
		{
			return nvrhi::BindingLayoutItem::StructuredBuffer_UAV(slot);
		}

		[[nodiscard]] nvrhi::BindingLayoutItem
		GetBindingLayoutItemSRV(uint32_t slot) const
		{
			return nvrhi::BindingLayoutItem::StructuredBuffer_SRV(slot);
		}

		[[nodiscard]] nvrhi::BindingSetItem
		GetBindingSetItemUAV(uint32_t slot) const
		{
			return nvrhi::BindingSetItem::StructuredBuffer_UAV(slot, m_buffer);
		}

		[[nodiscard]] nvrhi::BindingSetItem
		GetBindingSetItemSRV(uint32_t slot) const
		{
			return nvrhi::BindingSetItem::StructuredBuffer_SRV(slot, m_buffer);
		}

		void
		Update(nvrhi::CommandListHandle cmdList, std::span<const T> data)
		{
			if (data.size() * sizeof(T) > m_buffer->getDesc().byteSize)
			{
				throw GfxException{ GFX_RESULT_ERROR_DYNAMIC_BUFFER,
					                "GPULocalBuffer::Update",
					                "Data size exceeds buffer size." };
			}

			cmdList->writeBuffer(m_buffer, data.data(), data.size() * sizeof(T));
		}

		void
		SetResourceState(nvrhi::CommandListHandle cmdList, nvrhi::ResourceStates state) const
		{
			cmdList->setBufferState(m_buffer, state);
		}

		[[nodiscard]]
		nvrhi::IBuffer*
		GetBuffer() const
		{
			return m_buffer;
		}

	private:
		nvrhi::BufferHandle m_buffer;
	};
}
