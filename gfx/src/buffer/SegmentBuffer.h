#pragma once
#include "GfxException.h"
#include "buffer/types/SegmentID.h"
#include "frame_graph/FrameGraphView.h"
#include <core/type_traits.h>

namespace gfx
{
	struct SegmentBufferDesc final
	{
		uint32_t          startingCapacity      = 128;
		bool              useRedirectTableOnGPU = true;
		nvrhi::BufferDesc bufferDesc;
		nvrhi::BufferDesc redirectTableDesc;

		SegmentBufferDesc&
		SetStartingCapacity(uint32_t value)
		{
			startingCapacity = value;
			return *this;
		}

		SegmentBufferDesc&
		SetName(const std::string& value)
		{
			bufferDesc.setDebugName(value);
			redirectTableDesc.setDebugName(value + "_RedirectTable");
			return *this;
		}

		SegmentBufferDesc&
		SetIsVertexBuffer(bool value = true)
		{
			bufferDesc.setIsVertexBuffer(value);
			return *this;
		}

		SegmentBufferDesc&
		SetIsIndexBuffer(bool value = true)
		{
			bufferDesc.setIsIndexBuffer(value);
			return *this;
		}

		SegmentBufferDesc&
		SetUseRedirectTableOnGPU(bool value = true)
		{
			useRedirectTableOnGPU = value;
			return *this;
		}
	};

	template <typename T>
	concept SegmentBufferTConcept =
		core::type_traits::default_constructible<T> && core::type_traits::trivially_copyable<T>;

	/// <summary>
	/// Manages a GPU buffer with segmented storage and optional redirection table for indirect access. Provides efficient allocation, deallocation, and access to segments of structured data on the GPU.
	/// </summary>
	/// <typeparam name="T"></typeparam>
	template <SegmentBufferTConcept T>
	class SegmentBuffer final
	{
	public:
		using View                                       = FrameGraphView<SegmentBuffer<T>>;
		static constexpr uint32_t INVALID_PHYSICAL_INDEX = UINT32_MAX;

	private:
		struct Segment
		{
			SegmentID segmentId;
			uint32_t  count;
			uint32_t  physicalOffset;

			Segment() = default;
			Segment(SegmentID id, uint32_t cnt, uint32_t physOffset) :
				segmentId(id), count(cnt), physicalOffset(physOffset)
			{}
		};

	public:
		SegmentBuffer() noexcept = default;

		SegmentBuffer(nvrhi::DeviceHandle device, const SegmentBufferDesc& desc)
		{
			Init(device, desc);
		}

		void
		Init(nvrhi::DeviceHandle device, const SegmentBufferDesc& desc)
		{
			m_data.clear();
			m_segments.clear();

			m_segmentId2Idx.clear();
			m_segmentId2Idx.push_back(INVALID_PHYSICAL_INDEX);

			m_bufferDesc = desc.bufferDesc;
			m_bufferDesc.setStructStride(sizeof(T))
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true)
				.setByteSize(desc.startingCapacity * sizeof(T));

			m_buffer = device->createBuffer(m_bufferDesc);

			auto redirectDesc = desc.redirectTableDesc;
			redirectDesc.setStructStride(sizeof(uint32_t))
				.setKeepInitialState(true)
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setByteSize(desc.startingCapacity * sizeof(uint32_t));
			m_redirectTable = device->createBuffer(redirectDesc);

			m_dirty                 = true;
			m_useRedirectTableOnGPU = desc.useRedirectTableOnGPU;
		}

		[[nodiscard]] SegmentID
		EmplaceRange(std::span<const T> elements)
		{
			const uint32_t count = static_cast<uint32_t>(elements.size());
			if (count == 0)
				return 0;

			m_dirty = true;

			uint32_t physicalOffset = static_cast<uint32_t>(m_data.size());
			m_data.insert(m_data.end(), elements.begin(), elements.end());

			uint32_t segmentId = AllocateVirtualSpace(count);

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

			for (uint32_t i = 0; i < segment.count; ++i)
			{
				m_segmentId2Idx[segment.segmentId + i] = INVALID_PHYSICAL_INDEX;
			}

			m_segments.erase(it);
		}

		[[nodiscard]] T&
		At(SegmentID segmentId, uint32_t offset)
		{
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

			if (dataBytes > m_bufferDesc.byteSize)
			{
				m_bufferDesc.setByteSize(dataBytes * 2);
				m_buffer = device->createBuffer(m_bufferDesc);
			}

			cmdList->writeBuffer(m_buffer, m_data.data(), dataBytes);

			if (m_useRedirectTableOnGPU)
			{
				if (tableBytes > m_redirectTable->getDesc().byteSize)
				{
					nvrhi::BufferDesc redirectDesc = m_redirectTable->getDesc();
					redirectDesc.setByteSize(tableBytes * 2);
					m_redirectTable = device->createBuffer(redirectDesc);
				}
				cmdList->writeBuffer(m_redirectTable, m_segmentId2Idx.data(), tableBytes);
			}

			m_dirty = false;
			return true;
		}

		[[nodiscard]] nvrhi::BindingLayoutItem
		GetBindingLayoutItem(uint32_t slot) const
		{
			return nvrhi::BindingLayoutItem::StructuredBuffer_SRV(slot);
		}

		[[nodiscard]] nvrhi::BindingSetItem
		GetBindingSetItem(uint32_t slot) const
		{
			return nvrhi::BindingSetItem::StructuredBuffer_SRV(slot, m_buffer);
		}

		[[nodiscard]] nvrhi::BindingLayoutItem
		GetRedirectTableBindingLayoutItem(uint32_t slot) const
		{
			return nvrhi::BindingLayoutItem::StructuredBuffer_SRV(slot);
		}

		[[nodiscard]] nvrhi::BindingSetItem
		GetRedirectTableBindingSetItem(uint32_t slot) const
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
		bool                                   m_useRedirectTableOnGPU;
	};

	template <typename T>
	using SegmentBufferView = SegmentBuffer<T>::View;
}
