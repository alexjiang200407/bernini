#pragma once
#include "frame_graph/FrameGraphView.h"
#include <core/type_traits.h>

namespace gfx
{
	struct StructuredBufferDesc
	{
		uint32_t          startingCapacity = 128;
		nvrhi::BufferDesc bufferDesc;

		StructuredBufferDesc&
		SetStartingCapacity(uint32_t value)
		{
			startingCapacity = value;
			return *this;
		}

		StructuredBufferDesc&
		SetName(const std::string& value)
		{
			bufferDesc.setDebugName(value);
			return *this;
		}

		StructuredBufferDesc&
		SetIsVertexBuffer(bool value = true)
		{
			bufferDesc.setIsVertexBuffer(value);
			return *this;
		}

		StructuredBufferDesc&
		SetIsIndexBuffer(bool value = true)
		{
			bufferDesc.setIsIndexBuffer(value);
			return *this;
		}
	};

	template <core::type_traits::trivially_copyable T>
	class StructuredUploadBuffer
	{
	public:
		using View = FrameGraphView<StructuredUploadBuffer<T>>;

	public:
		StructuredUploadBuffer() noexcept                     = default;
		StructuredUploadBuffer(const StructuredUploadBuffer&) = delete;

		StructuredUploadBuffer(nvrhi::DeviceHandle device, const StructuredBufferDesc& desc)
		{
			Init(device, desc);
		}

		void
		Init(nvrhi::DeviceHandle device, const StructuredBufferDesc& desc)
		{
			m_data.reserve(desc.startingCapacity);

			m_bufferDesc = desc.bufferDesc;
			m_bufferDesc.setStructStride(sizeof(T))
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true);

			m_bufferDesc.byteSize = sizeof(T);
			m_buffer              = device->createBuffer(m_bufferDesc);

			dirty = false;
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
			dirty = true;
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
			if (!dirty || m_data.empty())
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
				dirty = false;
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
		bool                dirty = false;
	};

	template <typename T>
	using StructuredUploadBufferView = StructuredUploadBuffer<T>::View;
}
