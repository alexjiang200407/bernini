#pragma once
#include "GfxException.h"
#include "buffer/types/SegmentID.h"
#include "frame_graph/FrameGraphView.h"
#include <core/type_traits.h>

namespace gfx
{
	struct CPUUploadBufferDesc
	{
		uint32_t          startingCapacity      = 128;
		bool              useRedirectTableOnGPU = true;
		nvrhi::BufferDesc bufferDesc;
		nvrhi::BufferDesc redirectTableDesc;

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
			redirectTableDesc.setDebugName(value + "_RedirectTable");
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

		CPUUploadBufferDesc&
		SetUseRedirectTableOnGPU(bool value = true)
		{
			useRedirectTableOnGPU = value;
			return *this;
		}
	};

	template <typename T>
	concept CPUUploadBufferTConcept =
		core::type_traits::default_constructible<T> && core::type_traits::trivially_copyable<T>;

	template <CPUUploadBufferTConcept T>
	class CPUUploadBuffer final
	{
	public:
		using View                                       = FrameGraphView<CPUUploadBuffer<T>>;
		static constexpr uint32_t INVALID_PHYSICAL_INDEX = UINT32_MAX;

	private:
		struct Segment
		{
			SegmentID segmentId;  // This is the virtualOffset
			uint32_t  count;
			uint32_t  physicalOffset;

			Segment() = default;
			Segment(SegmentID id, uint32_t cnt, uint32_t physOffset) :
				segmentId(id), count(cnt), physicalOffset(physOffset)
			{}
		};

	public:
		CPUUploadBuffer() noexcept = default;

		void
		Init(nvrhi::DeviceHandle device, const CPUUploadBufferDesc& desc)
		{
			m_data.clear();
			m_segments.clear();

			// Slot 0 reserved for null/invalid
			m_segmentId2Idx.clear();
			m_segmentId2Idx.push_back(INVALID_PHYSICAL_INDEX);

			m_bufferDesc = desc.bufferDesc;
			m_bufferDesc.setStructStride(sizeof(T))
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setByteSize(desc.startingCapacity * sizeof(T));

			m_buffer = device->createBuffer(m_bufferDesc);

			auto redirectDesc = desc.redirectTableDesc;
			redirectDesc.setStructStride(sizeof(uint32_t))
				.setByteSize(desc.startingCapacity * sizeof(uint32_t));
			m_redirectTable = device->createBuffer(redirectDesc);

			m_dirty = true;
		}

		[[nodiscard]] SegmentID
		EmplaceRange(std::span<const T> elements)
		{
			const uint32_t count = static_cast<uint32_t>(elements.size());
			if (count == 0)
				return 0;

			m_dirty = true;

			// 1. Physical allocation: Always append to maintain order
			uint32_t physicalOffset = static_cast<uint32_t>(m_data.size());
			m_data.insert(m_data.end(), elements.begin(), elements.end());

			// 2. Virtual allocation: Find a contiguous block of IDs (slots)
			uint32_t segmentId = AllocateVirtualSpace(count);

			// 3. Update Redirect Table: Map virtual slots to physical indices
			if (segmentId + count > m_segmentId2Idx.size())
				m_segmentId2Idx.resize(segmentId + count, INVALID_PHYSICAL_INDEX);

			for (uint32_t i = 0; i < count; ++i)
			{
				m_segmentId2Idx[segmentId + i] = physicalOffset + i;
			}

			m_segments[segmentId] = Segment(segmentId, count, physicalOffset);
			return segmentId;
		}

		void
		Erase(SegmentID segmentId)
		{
			auto it = m_segments.find(segmentId);
			if (it == m_segments.end())
				return;

			m_dirty             = true;
			const auto& segment = it->second;

			// Mark slots as invalid in the redirect table
			// We do NOT move physical data here to preserve order
			for (uint32_t i = 0; i < segment.count; ++i)
			{
				m_segmentId2Idx[segment.segmentId + i] = INVALID_PHYSICAL_INDEX;
			}

			m_segments.erase(it);
		}

		[[nodiscard]] T&
		At(SegmentID segmentId, uint32_t offset)
		{
			// Direct lookup: segmentId is the base in our redirect table
			uint32_t physicalIndex = m_segmentId2Idx[segmentId + offset];
			m_dirty                = true;
			return m_data[physicalIndex];
		}

		bool
		Update(nvrhi::CommandListHandle cmdList, nvrhi::DeviceHandle device)
		{
			if (!m_dirty)
				return false;

			const size_t dataBytes  = m_data.size() * sizeof(T);
			const size_t tableBytes = m_segmentId2Idx.size() * sizeof(uint32_t);

			// Resize GPU buffers if CPU vectors grew
			if (dataBytes > m_bufferDesc.byteSize)
			{
				m_bufferDesc.setByteSize(dataBytes * 1.5);
				m_buffer = device->createBuffer(m_bufferDesc);
			}

			cmdList->writeBuffer(m_buffer, m_data.data(), dataBytes);
			cmdList->writeBuffer(m_redirectTable, m_segmentId2Idx.data(), tableBytes);

			m_dirty = false;
			return true;
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

	private:
		uint32_t
		AllocateVirtualSpace(uint32_t count)
		{
			uint32_t consecutiveFree = 0;
			for (uint32_t i = 1; i < m_segmentId2Idx.size(); ++i)
			{
				if (m_segmentId2Idx[i] == INVALID_PHYSICAL_INDEX)
				{
					if (++consecutiveFree == count)
						return i - count + 1;
				}
				else
					consecutiveFree = 0;
			}
			return static_cast<uint32_t>(m_segmentId2Idx.size());
		}

		nvrhi::BufferDesc                      m_bufferDesc;
		nvrhi::BufferHandle                    m_buffer;
		nvrhi::BufferHandle                    m_redirectTable;
		std::vector<T>                         m_data;
		std::vector<uint32_t>                  m_segmentId2Idx;
		std::unordered_map<SegmentID, Segment> m_segments;
		bool                                   m_dirty = false;
	};

	template <typename T>
	using CPUUploadBufferView = CPUUploadBuffer<T>::View;
}
