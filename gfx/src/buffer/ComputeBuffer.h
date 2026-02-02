#pragma once

namespace gfx
{
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

		uint32_t              elemSize  = 0;
		uint32_t              elemCount = 0;
		std::string           debugName;
		nvrhi::ResourceStates initialState = nvrhi::ResourceStates::UnorderedAccess;
	};

	class ComputeBuffer final
	{
	public:
		ComputeBuffer()                     = default;
		ComputeBuffer(const ComputeBuffer&) = delete;
		ComputeBuffer(nvrhi::DeviceHandle device, const ComputeBufferDesc& desc)
		{
			Init(device, desc);
		}

		const ComputeBuffer&
		operator=(const ComputeBuffer&) = delete;

		void
		Init(nvrhi::DeviceHandle device, const ComputeBufferDesc& desc)
		{
			auto bufferDesc = nvrhi::BufferDesc{};
			bufferDesc.setStructStride(desc.elemSize)
				.setByteSize(static_cast<uint64_t>(desc.elemSize) * desc.elemCount)
				.setCanHaveUAVs(true)
				.setDebugName(desc.debugName)
				.setInitialState(nvrhi::ResourceStates::UnorderedAccess);
			m_buffer = device->createBuffer(bufferDesc);
		}

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
		GetBindingLayoutItem(uint32_t slot) const
		{
			return nvrhi::BindingLayoutItem::StructuredBuffer_UAV(slot);
		}

		[[nodiscard]] nvrhi::BindingSetItem
		GetBindingSetItem(uint32_t slot) const
		{
			return nvrhi::BindingSetItem::StructuredBuffer_UAV(slot, m_buffer);
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
		ToIndirectArgResourceState(nvrhi::CommandListHandle cmdList)
		{
			cmdList->setBufferState(m_buffer, nvrhi::ResourceStates::IndirectArgument);
		}

		void
		ToUnorderedAccessState(nvrhi::CommandListHandle cmdList)
		{
			cmdList->setBufferState(m_buffer, nvrhi::ResourceStates::UnorderedAccess);
		}

	private:
		uint32_t            m_elementCount = 0;
		uint32_t            m_elementSize  = 0;
		bool                m_dirty        = false;
		nvrhi::BufferHandle m_buffer;
	};
}
