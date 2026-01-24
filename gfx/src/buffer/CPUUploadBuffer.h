#pragma once
#include "frame_graph/FrameGraphView.h"
#include <core/type_traits.h>

namespace gfx
{
	struct CPUUploadBufferDesc
	{
		uint32_t          startingCapacity = 128;
		nvrhi::BufferDesc bufferDesc;

		CPUUploadBufferDesc&
		SetStartingCapacity(uint32_t value)
		{
			startingCapacity = value;
			return *this;
		}

		CPUUploadBufferDesc&
		SetName(const std::string& value)
		{
			bufferDesc.setDebugName(value);
			return *this;
		}

		CPUUploadBufferDesc&
		SetIsVertexBuffer(bool value = true)
		{
			bufferDesc.setIsVertexBuffer(value);
			return *this;
		}

		CPUUploadBufferDesc&
		SetIsIndexBuffer(bool value = true)
		{
			bufferDesc.setIsIndexBuffer(value);
			return *this;
		}
	};

	template <core::type_traits::trivially_copyable T>
	class CPUUploadBuffer
	{
	public:
		using View = FrameGraphView<CPUUploadBuffer<T>>;

	public:
		CPUUploadBuffer() noexcept              = default;
		CPUUploadBuffer(const CPUUploadBuffer&) = delete;

		CPUUploadBuffer(nvrhi::DeviceHandle device, const CPUUploadBufferDesc& desc)
		{
			Init(device, desc);
		}

		void
		Init(nvrhi::DeviceHandle device, const CPUUploadBufferDesc& desc)
		{
			m_data.reserve(desc.startingCapacity);

			m_bufferDesc = desc.bufferDesc;
			m_bufferDesc.setStructStride(sizeof(T))
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true)
				.setByteSize(sizeof(T));

			m_buffer = device->createBuffer(m_bufferDesc);

			m_dirty = false;
		}

		size_t
		Size() const noexcept
		{
			return m_data.size();
		}

		template <typename... Args>
		uint32_t
		Emplace(Args&&... args)
		{
			m_dirty = true;
			m_data.emplace_back(std::forward<Args>(args)...);
			return static_cast<uint32_t>(m_data.size() - 1);
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

		[[nodiscard]]
		bool
		Update(nvrhi::CommandListHandle cmdList, nvrhi::DeviceHandle device)
		{
			if (!m_dirty || m_data.empty())
				return false;

			const auto     requiredBytes    = m_data.size() * sizeof(T);
			auto           recreatedBuffer  = false;
			constexpr auto bufferGrowthRate = 2;

			if (requiredBytes > m_bufferDesc.byteSize)
			{
				m_bufferDesc.setByteSize(requiredBytes * bufferGrowthRate);
				m_buffer        = device->createBuffer(m_bufferDesc);
				recreatedBuffer = true;
			}

			if (requiredBytes > 0)
			{
				cmdList->writeBuffer(m_buffer, m_data.data(), requiredBytes);
				m_dirty = false;
			}

			return recreatedBuffer;
		}

		nvrhi::IBuffer*
		GetBuffer() const
		{
			return m_buffer;
		}

	private:
		nvrhi::BufferDesc   m_bufferDesc;
		nvrhi::BufferHandle m_buffer;
		std::vector<T>      m_data;
		bool                m_dirty = false;
	};

	template <typename T>
	using CPUUploadBufferView = CPUUploadBuffer<T>::View;
}
