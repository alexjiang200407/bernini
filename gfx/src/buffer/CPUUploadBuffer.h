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

	private:
		struct FreeBlock
		{
			uint32_t startingIndex;
			uint32_t count;

			bool
			operator<(const FreeBlock& other) const
			{
				return startingIndex < other.startingIndex;
			}
		};

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
			m_data.clear();
			m_freeBlocks.clear();

			m_data.reserve(desc.startingCapacity);

			m_data.emplace_back(T{});

			m_bufferDesc = desc.bufferDesc;
			m_bufferDesc.setStructStride(sizeof(T))
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true)
				.setByteSize(m_data.size() * sizeof(T));

			m_buffer = device->createBuffer(m_bufferDesc);
			m_dirty  = true;
		}

		[[nodiscard]] uint32_t
		Size() const noexcept
		{
			return static_cast<uint32_t>(m_data.size());
		}

		template <typename... Args>
		[[nodiscard]] uint32_t
		Emplace(Args&&... args)
		{
			T element(std::forward<Args>(args)...);
			return EmplaceRange(std::span<const T>(&element, 1));
		}

		[[nodiscard]] uint32_t
		EmplaceRange(std::span<const T> elements)
		{
			const uint32_t count = static_cast<uint32_t>(elements.size());
			if (count == 0)
				return 0;

			m_dirty = true;

			for (auto it = m_freeBlocks.begin(); it != m_freeBlocks.end(); ++it)
			{
				if (it->count >= count)
				{
					uint32_t offset = it->startingIndex;

					std::copy(elements.begin(), elements.end(), m_data.begin() + offset);

					if (it->count == count)
					{
						m_freeBlocks.erase(it);
					}
					else
					{
						it->startingIndex += count;
						it->count -= count;
					}
					return offset;
				}
			}

			uint32_t startingIndex = static_cast<uint32_t>(m_data.size());
			m_data.insert(m_data.end(), elements.begin(), elements.end());

			return startingIndex;
		}

		void
		EraseRange(uint32_t startingIndex, uint32_t count)
		{
			if (count == 0 || startingIndex == 0)
				return;
			m_dirty = true;

			auto it = std::lower_bound(
				m_freeBlocks.begin(),
				m_freeBlocks.end(),
				FreeBlock{ startingIndex, count });

			bool mergedNext = false;
			if (it != m_freeBlocks.end())
			{
				if (startingIndex + count == it->startingIndex)
				{
					it->startingIndex = startingIndex;
					it->count += count;
					mergedNext = true;
				}
			}

			if (it != m_freeBlocks.begin())
			{
				auto prev = std::prev(it);
				if (prev->startingIndex + prev->count == startingIndex)
				{
					if (mergedNext)
					{
						prev->count += it->count;
						m_freeBlocks.erase(it);
					}
					else
					{
						prev->count += count;
					}
					return;
				}
			}

			if (!mergedNext)
			{
				m_freeBlocks.insert(it, { startingIndex, count });
			}
		}

		void
		Erase(uint32_t index)
		{
			EraseRange(index, 1);
		}

		[[nodiscard]] const T&
		At(uint32_t index) const
		{
			if (index >= m_data.size())
			{
				throw GfxException(
					GFX_RESULT_ERROR_CPU_UPLOAD_BUFFER,
					"CPUUploadBuffer::At - Index out of bounds",
					"Attempted to access index " + std::to_string(index) +
						" but the buffer only contains " + std::to_string(m_data.size()) +
						" elements.");
			}

			return m_data[index];
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
		nvrhi::BufferDesc      m_bufferDesc;
		nvrhi::BufferHandle    m_buffer;
		std::vector<T>         m_data;
		std::vector<FreeBlock> m_freeBlocks;
		bool                   m_dirty = false;
	};

	template <typename T>
	using CPUUploadBufferView = CPUUploadBuffer<T>::View;
}
