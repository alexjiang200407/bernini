#pragma once
#include "frame_graph/FrameGraphView.h"
#include <core/type_traits.h>

namespace gfx
{
	struct StructuredBufferUAVDesc
	{
		uint32_t startingLen = 1;

		nvrhi::BufferDesc bufferDesc;

		StructuredBufferUAVDesc&
		SetName(const std::string& val)
		{
			bufferDesc.setDebugName(val);
			return *this;
		}

		StructuredBufferUAVDesc&
		SetStartingLen(uint32_t val)
		{
			startingLen = val;
			return *this;
		}

		StructuredBufferUAVDesc&
		SetIsDrawIndirect(bool isDrawIndirect = true)
		{
			bufferDesc.setIsDrawIndirectArgs(isDrawIndirect);
			return *this;
		}

		StructuredBufferUAVDesc&
		SetKeepInitialState(bool keepInitial = true)
		{
			bufferDesc.setKeepInitialState(keepInitial);
			return *this;
		}

		StructuredBufferUAVDesc&
		SetInitialState(nvrhi::ResourceStates state)
		{
			bufferDesc.setInitialState(state);
			return *this;
		}
	};

	template <core::type_traits::trivially_copyable T>
	class StructuredBufferUAV
	{
	public:
		using View = FrameGraphView<StructuredBufferUAV<T>>;

	public:
		StructuredBufferUAV() noexcept                  = default;
		StructuredBufferUAV(const StructuredBufferUAV&) = delete;

		StructuredBufferUAV(nvrhi::DeviceHandle device, const StructuredBufferUAVDesc& desc)
		{
			Init(device, desc);
		}

		void
		Init(nvrhi::DeviceHandle device, const StructuredBufferUAVDesc& desc)
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
		GetBindingLayoutItem(uint32_t slot) const
		{
			return nvrhi::BindingLayoutItem::StructuredBuffer_UAV(slot);
		}

		[[nodiscard]] nvrhi::BindingSetItem
		GetBindingSetItem(uint32_t slot) const
		{
			return nvrhi::BindingSetItem::StructuredBuffer_UAV(slot, m_buffer);
		}

		void
		Update(nvrhi::CommandListHandle cmdList, std::span<const T> data)
		{
			if (data.size() * sizeof(T) > m_buffer->getDesc().byteSize)
			{
				throw GfxException{ GFX_RESULT_ERROR_DYNAMIC_BUFFER,
					                "StructuredBufferUAV::Update",
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
