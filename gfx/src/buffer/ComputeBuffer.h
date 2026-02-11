#pragma once

namespace gfx
{
	struct ComputeBufferDesc;

	class ComputeBuffer final
	{
	public:
		ComputeBuffer() = default;
		ComputeBuffer(nvrhi::DeviceHandle device, const ComputeBufferDesc& desc)
		{
			Init(device, desc);
		}

		void
		Init(nvrhi::DeviceHandle device, const ComputeBufferDesc& desc);

		[[nodiscard]] uint32_t
		Size() const
		{
			return m_elementCount;
		}

		void
		Resize(uint32_t size)
		{
			m_dirty        = true;
			m_elementCount = size;
		}

		[[nodiscard]] nvrhi::BindingLayoutItem
		GetBindingLayoutItemUAV(uint32_t slot) const
		{
			return nvrhi::BindingLayoutItem::StructuredBuffer_UAV(slot);
		}

		[[nodiscard]] nvrhi::BindingSetItem
		GetBindingSetItemUAV(uint32_t slot) const
		{
			return nvrhi::BindingSetItem::StructuredBuffer_UAV(slot, m_buffer);
		}

		[[nodiscard]] nvrhi::BindingLayoutItem
		GetBindingLayoutItemSRV(uint32_t slot) const
		{
			return nvrhi::BindingLayoutItem::StructuredBuffer_SRV(slot);
		}

		[[nodiscard]] nvrhi::BindingSetItem
		GetBindingSetItemSRV(uint32_t slot) const
		{
			return nvrhi::BindingSetItem::StructuredBuffer_SRV(slot, m_buffer);
		}

		[[nodiscard]] nvrhi::IBuffer*
		GetBuffer() const
		{
			return m_buffer;
		}

		bool
		Update(nvrhi::CommandListHandle cmdList, nvrhi::DeviceHandle device)
		{
			if (m_dirty)
			{
				auto desc = m_buffer->getDesc();
				if (m_elementCount * m_elementSize > desc.byteSize)
				{
					desc.setByteSize(static_cast<uint64_t>(m_elementCount) * m_elementSize);
					m_buffer = device->createBuffer(desc);
				}
				m_dirty = false;
				return true;
			}

			return false;
		}

		void
		Clear(nvrhi::CommandListHandle cmdList)
		{
			cmdList->clearBufferUInt(m_buffer, 0);
		}

		void
		TransitionState(nvrhi::CommandListHandle cmdList, nvrhi::ResourceStates state)
		{
			cmdList->setBufferState(m_buffer, state);
		}

		void
		TrackResourceState(nvrhi::CommandListHandle cmdList, nvrhi::ResourceStates state) const
		{
			cmdList->beginTrackingBufferState(m_buffer, state);
		}

	private:
		uint32_t            m_elementCount = 0;
		uint32_t            m_elementSize  = 0;
		bool                m_dirty        = false;
		nvrhi::BufferHandle m_buffer;
	};

	struct ComputeBufferDesc
	{
		ComputeBufferDesc&
		SetElementSize(uint32_t size)
		{
			elemSize = size;
			return *this;
		}

		template <typename T>
		ComputeBufferDesc&
		SetElement()
		{
			elemSize = sizeof(T);
			return *this;
		}

		ComputeBufferDesc&
		SetElementCount(uint32_t size)
		{
			elemCount = size;
			return *this;
		}

		ComputeBufferDesc&
		SetName(const std::string& name)
		{
			debugName = name;
			return *this;
		}

		ComputeBufferDesc&
		SetInitialState(nvrhi::ResourceStates state)
		{
			initialState = state;
			return *this;
		}

		ComputeBufferDesc&
		SetKeepInitialState(bool keepInitialState = true)
		{
			this->keepInitialState = keepInitialState;
			return *this;
		}

		ComputeBuffer
		Create(nvrhi::DeviceHandle device) const
		{
			return ComputeBuffer{ device, *this };
		}

		uint32_t              elemSize  = 0;
		uint32_t              elemCount = 0;
		nvrhi::ResourceStates initialState;
		bool                  keepInitialState = false;
		std::string           debugName;
	};

}
