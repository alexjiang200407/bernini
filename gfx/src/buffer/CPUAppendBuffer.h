#pragma once
#include "GfxException.h"
#include "frame_graph/FrameGraphView.h"
#include <core/type_traits.h>

namespace gfx
{
	struct CPUAppendBufferDesc
	{
		uint32_t          startingCapacity      = 128;
		bool              useRedirectTableOnGPU = true;
		nvrhi::BufferDesc bufferDesc;
		nvrhi::BufferDesc redirectTableDesc;

		CPUAppendBufferDesc&
		SetStartingCapacity(uint32_t value)
		{
			startingCapacity = value;
			return *this;
		}
		CPUAppendBufferDesc&
		SetName(const std::string& value)
		{
			bufferDesc.setDebugName(value);
			redirectTableDesc.setDebugName(value + "_RedirectTable");
			return *this;
		}
		CPUAppendBufferDesc&
		SetIsVertexBuffer(bool value = true)
		{
			bufferDesc.setIsVertexBuffer(value);
			return *this;
		}
		CPUAppendBufferDesc&
		SetIsIndexBuffer(bool value = true)
		{
			bufferDesc.setIsIndexBuffer(value);
			return *this;
		}
		CPUAppendBufferDesc&
		SetUseRedirectTableOnGPU(bool value = true)
		{
			useRedirectTableOnGPU = value;
			return *this;
		}
	};

	template <typename T>
	concept CPUAppendBufferTConcept =
		core::type_traits::default_constructible<T> && core::type_traits::trivially_copyable<T>;

	template <CPUAppendBufferTConcept T>
	class CPUAppendBuffer final
	{
	public:
		using View = FrameGraphView<CPUAppendBuffer<T>>;

		// Invalid physical index marker
		static constexpr uint32_t INVALID_PHYSICAL_INDEX = UINT32_MAX;

		CPUAppendBuffer() noexcept              = default;
		CPUAppendBuffer(const CPUAppendBuffer&) = delete;
		CPUAppendBuffer(nvrhi::DeviceHandle device, const CPUAppendBufferDesc& desc) :
			m_useRedirectTableOnGPU{ desc.useRedirectTableOnGPU }
		{
			Init(device, desc);
		}

		void
		Init(nvrhi::DeviceHandle device, const CPUAppendBufferDesc& desc)
		{
			m_data.clear();
			m_data.reserve(desc.startingCapacity);
			m_data.emplace_back(T{});  // Index 0 reserved

			m_v2p.clear();
			m_v2p.reserve(desc.startingCapacity);
			m_v2p.emplace_back(0);  // Virtual index 0 -> Physical index 0

			m_p2v.clear();
			m_p2v.reserve(desc.startingCapacity);
			m_p2v.emplace_back(0);  // Physical index 0 -> Virtual index 0

			// Create main data buffer
			auto bufferDesc = desc.bufferDesc;
			bufferDesc.setStructStride(sizeof(T))
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true)
				.setByteSize(m_data.size() * sizeof(T));
			m_buffer = device->createBuffer(bufferDesc);

			// Create redirect table buffer
			auto redirectDesc = desc.redirectTableDesc;
			redirectDesc.setStructStride(sizeof(uint32_t))
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true)
				.setByteSize(m_v2p.size() * sizeof(uint32_t));
			m_redirectTable = device->createBuffer(redirectDesc);

			m_dirty = true;
		}

		void
		Erase(uint32_t virtualIndex)
		{
			if (virtualIndex == 0 || virtualIndex >= m_v2p.size())
				throw GfxException(
					GFX_RESULT_ERROR_CPU_APPEND_BUFFER,
					"CPUAppendBuffer::Erase",
					"Invalid Virtual Index");

			uint32_t physicalIndex = m_v2p[virtualIndex];
			if (physicalIndex == INVALID_PHYSICAL_INDEX || physicalIndex == 0 ||
			    physicalIndex >= m_data.size())
				throw GfxException(
					GFX_RESULT_ERROR_CPU_APPEND_BUFFER,
					"CPUAppendBuffer::Erase",
					"Invalid Virtual Index");

			m_dirty = true;

			// Swap with last element in physical storage
			uint32_t lastPhysicalIndex = static_cast<uint32_t>(m_data.size() - 1);

			if (physicalIndex != lastPhysicalIndex)
			{
				// Move last element to the deleted position
				m_data[physicalIndex] = m_data[lastPhysicalIndex];

				// Update the virtual index that pointed to the last element
				uint32_t lastVirtualIndex = m_p2v[lastPhysicalIndex];
				m_v2p[lastVirtualIndex]   = physicalIndex;
				m_p2v[physicalIndex]      = lastVirtualIndex;
			}

			m_data.pop_back();
			m_p2v.pop_back();

			// Mark virtual index as free
			m_v2p[virtualIndex] = INVALID_PHYSICAL_INDEX;
		}

		template <typename... Args>
		uint32_t
		EmplaceBack(Args&&... args)
		{
			m_dirty = true;

			// Allocate physical index
			uint32_t physicalIndex = static_cast<uint32_t>(m_data.size());
			m_data.emplace_back(std::forward<Args>(args)...);

			// Find first free virtual index (OS-style first-fit)
			uint32_t virtualIndex = AllocateVirtualIndex();

			// Update mappings
			if (virtualIndex < m_v2p.size())
			{
				m_v2p[virtualIndex] = physicalIndex;
			}
			else
			{
				m_v2p.push_back(physicalIndex);
			}

			m_p2v.push_back(virtualIndex);

			return virtualIndex;
		}

		[[nodiscard]] uint32_t
		Size() const noexcept
		{
			return static_cast<uint32_t>(m_data.size());
		}

		[[nodiscard]] uint32_t
		VirtualSize() const noexcept
		{
			return static_cast<uint32_t>(m_v2p.size());
		}

		[[nodiscard]] const T&
		At(uint32_t virtualIndex) const
		{
			if (virtualIndex >= m_v2p.size())
			{
				throw GfxException(
					GFX_RESULT_ERROR_CPU_APPEND_BUFFER,
					"CPUAppendBuffer::At",
					"Invalid Virtual Index");
			}
			uint32_t physicalIndex = m_v2p[virtualIndex];
			if (physicalIndex == INVALID_PHYSICAL_INDEX || physicalIndex >= m_data.size())
			{
				throw GfxException(
					GFX_RESULT_ERROR_CPU_APPEND_BUFFER,
					"CPUAppendBuffer::At",
					"Invalid Virtual Index");
			}
			return m_data[physicalIndex];
		}

		[[nodiscard]] T&
		At(uint32_t virtualIndex)
		{
			if (virtualIndex >= m_v2p.size())
			{
				throw GfxException(
					GFX_RESULT_ERROR_CPU_APPEND_BUFFER,
					"CPUAppendBuffer::At",
					"Invalid Virtual Index");
			}
			uint32_t physicalIndex = m_v2p[virtualIndex];
			if (physicalIndex == INVALID_PHYSICAL_INDEX || physicalIndex >= m_data.size())
			{
				throw GfxException(
					GFX_RESULT_ERROR_CPU_APPEND_BUFFER,
					"CPUAppendBuffer::At",
					"Invalid Virtual Index");
			}
			m_dirty = true;  // Assume mutation
			return m_data[physicalIndex];
		}

		[[nodiscard]] bool
		IsValid(uint32_t virtualIndex) const noexcept
		{
			return virtualIndex < m_v2p.size() && m_v2p[virtualIndex] != INVALID_PHYSICAL_INDEX &&
			       m_v2p[virtualIndex] < m_data.size();
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

		[[nodiscard]] nvrhi::BindingLayoutItem
		GetRedirectTableBindingLayoutItemSRV(uint32_t slot) const
		{
			return nvrhi::BindingLayoutItem::StructuredBuffer_SRV(slot);
		}

		[[nodiscard]] nvrhi::BindingSetItem
		GetRedirectTableBindingSetItemSRV(uint32_t slot) const
		{
			return nvrhi::BindingSetItem::StructuredBuffer_SRV(slot, m_redirectTable);
		}

		[[nodiscard]]
		bool
		Update(nvrhi::CommandListHandle cmdList, nvrhi::DeviceHandle device)
		{
			if (!m_dirty)
				return false;

			const size_t requiredDataSize = m_data.size() * sizeof(T);
			const size_t requiredV2PSize  = m_v2p.size() * sizeof(uint32_t);
			bool         recreated        = false;

			// Update main data buffer
			auto desc = m_buffer->getDesc();
			if (!m_buffer || requiredDataSize > desc.byteSize)
			{
				size_t newSize = requiredDataSize;
				if (m_buffer)
				{
					newSize =
						std::max(requiredDataSize, (size_t)(m_buffer->getDesc().byteSize * 1.5));
				}
				desc.setByteSize(newSize);
				m_buffer  = device->createBuffer(desc);
				recreated = true;
			}
			cmdList->writeBuffer(m_buffer, m_data.data(), requiredDataSize);

			if (m_useRedirectTableOnGPU)
			{
				// Update redirect table buffer
				auto redirectDesc = m_redirectTable->getDesc();
				if (!m_redirectTable || requiredV2PSize > redirectDesc.byteSize)
				{
					size_t newSize = requiredV2PSize;
					if (m_redirectTable)
					{
						newSize = std::max(
							requiredV2PSize,
							static_cast<size_t>(m_redirectTable->getDesc().byteSize * 1.5));
					}

					redirectDesc.setByteSize(newSize);
					if (!desc.debugName.empty())
					{
						redirectDesc.setDebugName(desc.debugName + "_RedirectTable");
					}
					m_redirectTable = device->createBuffer(redirectDesc);
					recreated       = true;
				}
				cmdList->writeBuffer(m_redirectTable, m_v2p.data(), requiredV2PSize);
			}

			m_dirty = false;
			return recreated;
		}

		nvrhi::IBuffer*
		GetBuffer() const
		{
			return m_buffer;
		}

		nvrhi::IBuffer*
		GetRedirectTable() const
		{
			return m_redirectTable;
		}

	private:
		// TODO: Use a more optimized system using bit flag vector with each flag representing 512 indices
		// OS-style first-fit allocation
		uint32_t
		AllocateVirtualIndex()
		{
			// Search for first free virtual index starting from 1 (0 is reserved)
			for (uint32_t i = 1; i < m_v2p.size(); ++i)
			{
				if (m_v2p[i] == INVALID_PHYSICAL_INDEX)
				{
					return i;
				}
			}
			// No free index found, return next available
			return static_cast<uint32_t>(m_v2p.size());
		}

		std::vector<T>        m_data;    // Physical storage [physical_index] -> data
		std::vector<uint32_t> m_v2p;     // Virtual to Physical [virtual_index] -> physical_index
		std::vector<uint32_t> m_p2v;     // Physical to Virtual [physical_index] -> virtual_index
		nvrhi::BufferHandle   m_buffer;  // Main data buffer (GPU)
		nvrhi::BufferHandle   m_redirectTable;
		bool                  m_dirty = false;
		bool                  m_useRedirectTableOnGPU;
	};

	template <typename T>
	using CPUAppendBufferView = CPUAppendBuffer<T>::View;
}
