#pragma once
#include "error/GfxException.h"
#include "frame_graph/FrameGraphView.h"
#include <core/type_traits.h>

namespace gfx
{
	struct AppendBufferDesc
	{
		uint32_t          startingCapacity      = 128;
		bool              useRedirectTableOnGPU = true;
		nvrhi::BufferDesc bufferDesc;
		nvrhi::BufferDesc redirectTableDesc;

		AppendBufferDesc&
		SetStartingCapacity(uint32_t value)
		{
			startingCapacity = value;
			return *this;
		}
		AppendBufferDesc&
		SetName(const std::string& value)
		{
			bufferDesc.setDebugName(value);
			redirectTableDesc.setDebugName(value + "_RedirectTable");
			return *this;
		}
		AppendBufferDesc&
		SetIsVertexBuffer(bool value = true)
		{
			bufferDesc.setIsVertexBuffer(value);
			return *this;
		}
		AppendBufferDesc&
		SetIsIndexBuffer(bool value = true)
		{
			bufferDesc.setIsIndexBuffer(value);
			return *this;
		}
		AppendBufferDesc&
		SetUseRedirectTableOnGPU(bool value = true)
		{
			useRedirectTableOnGPU = value;
			return *this;
		}
	};

	template <typename T>
	concept AppendBufferTConcept =
		core::type_traits::default_constructible<T> && core::type_traits::trivially_copyable<T>;

	struct NoExtraData
	{};

	template <typename T>
	concept ExtraDataConcept = core::type_traits::is_nothrow_copy_assignable<T> &&
	                           core::type_traits::is_nothrow_destructible<T> &&
	                           core::type_traits::default_constructible<T>;

	/// <summary>
	/// A GPU-backed buffer that maintains stable virtual indices while allowing efficient element removal through physical index compaction. The items must be atomic, no order
	/// </summary>
	/// <typeparam name="T">GPU data type (uploaded to GPU)</typeparam>
	/// <typeparam name="ExtraData">CPU-local data type (not uploaded to GPU)</typeparam>
	template <AppendBufferTConcept T, ExtraDataConcept ExtraData = NoExtraData>
	class AppendBuffer final
	{
	public:
		using View = FrameGraphView<AppendBuffer<T, ExtraData>>;

		static constexpr uint32_t INVALID_PHYSICAL_INDEX = UINT32_MAX;

		AppendBuffer() noexcept           = default;
		AppendBuffer(const AppendBuffer&) = delete;
		AppendBuffer(nvrhi::DeviceHandle device, const AppendBufferDesc& desc) :
			m_useRedirectTableOnGPU{ desc.useRedirectTableOnGPU }
		{
			Init(device, desc);
		}

		void
		Init(nvrhi::DeviceHandle device, const AppendBufferDesc& desc)
		{
			m_data.clear();
			m_data.reserve(desc.startingCapacity);
			m_data.emplace_back(T{});

			if constexpr (!std::is_same_v<ExtraData, NoExtraData>)
			{
				m_extraData.clear();
				m_extraData.reserve(desc.startingCapacity);
				m_extraData.emplace_back(ExtraData{});
			}

			m_id2Idx.clear();
			m_id2Idx.reserve(desc.startingCapacity);
			m_id2Idx.emplace_back(0);

			m_idx2Id.clear();
			m_idx2Id.reserve(desc.startingCapacity);
			m_idx2Id.emplace_back(0);

			auto bufferDesc = desc.bufferDesc;
			bufferDesc.setStructStride(sizeof(T))
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true)
				.setByteSize(m_data.size() * sizeof(T));
			m_buffer = device->createBuffer(bufferDesc);

			auto redirectDesc = desc.redirectTableDesc;
			redirectDesc.setStructStride(sizeof(uint32_t))
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true)
				.setByteSize(m_id2Idx.size() * sizeof(uint32_t));
			m_redirectTable = device->createBuffer(redirectDesc);

			m_dirty = true;
		}

		bool
		Erase(uint32_t virtualIndex) noexcept
		{
			if (virtualIndex == 0 || virtualIndex >= m_id2Idx.size())
				return false;

			uint32_t physicalIndex = m_id2Idx[virtualIndex];
			if (physicalIndex == INVALID_PHYSICAL_INDEX || physicalIndex == 0 ||
			    physicalIndex >= m_data.size())
				return false;

			m_dirty = true;

			auto lastPhysicalIndex = static_cast<uint32_t>(m_data.size() - 1);

			if (physicalIndex != lastPhysicalIndex)
			{
				m_data[physicalIndex] = m_data[lastPhysicalIndex];

				if constexpr (!std::is_same_v<ExtraData, NoExtraData>)
				{
					m_extraData[physicalIndex] = m_extraData[lastPhysicalIndex];
				}

				uint32_t lastVirtualIndex  = m_idx2Id[lastPhysicalIndex];
				m_id2Idx[lastVirtualIndex] = physicalIndex;
				m_idx2Id[physicalIndex]    = lastVirtualIndex;
			}

			m_data.pop_back();
			if constexpr (!std::is_same_v<ExtraData, NoExtraData>)
			{
				m_extraData.pop_back();
			}
			m_idx2Id.pop_back();

			m_id2Idx[virtualIndex] = INVALID_PHYSICAL_INDEX;

			return true;
		}

		template <typename... Args>
		uint32_t
		EmplaceBack(Args&&... args)
		{
			m_dirty = true;

			uint32_t physicalIndex = static_cast<uint32_t>(m_data.size());
			m_data.emplace_back(std::forward<Args>(args)...);

			if constexpr (!std::is_same_v<ExtraData, NoExtraData>)
			{
				m_extraData.emplace_back(ExtraData{});
			}

			uint32_t virtualIndex = AllocateVirtualIndex();

			if (virtualIndex < m_id2Idx.size())
			{
				m_id2Idx[virtualIndex] = physicalIndex;
			}
			else
			{
				m_id2Idx.push_back(physicalIndex);
			}

			m_idx2Id.push_back(virtualIndex);

			return virtualIndex;
		}

		// Emplace with both GPU data and CPU-local extra data
		template <typename... TArgs, typename... ExtraArgs>
		uint32_t
		EmplaceBackWithExtra(std::tuple<TArgs...>&& tArgs, std::tuple<ExtraArgs...>&& extraArgs)
			requires(!std::is_same_v<ExtraData, NoExtraData>)
		{
			m_dirty = true;

			uint32_t physicalIndex = static_cast<uint32_t>(m_data.size());

			m_data.emplace_back(std::make_from_tuple<T>(std::move(tArgs)));
			m_extraData.emplace_back(std::make_from_tuple<ExtraData>(std::move(extraArgs)));

			uint32_t virtualIndex = AllocateVirtualIndex();

			if (virtualIndex < m_id2Idx.size())
			{
				m_id2Idx[virtualIndex] = physicalIndex;
			}
			else
			{
				m_id2Idx.push_back(physicalIndex);
			}

			m_idx2Id.push_back(virtualIndex);

			return virtualIndex;
		}

		[[nodiscard]] uint32_t
		Size() const noexcept
		{
			return static_cast<uint32_t>(m_data.size());
		}

		[[nodiscard]] const T&
		At(uint32_t virtualIndex) const
		{
			if (virtualIndex >= m_id2Idx.size())
			{
				THROW_GFX_ERROR(
					"AppendBuffer::At",
					"Invalid Virtual Index");
			}
			uint32_t physicalIndex = m_id2Idx[virtualIndex];
			if (physicalIndex == INVALID_PHYSICAL_INDEX || physicalIndex >= m_data.size())
			{
				THROW_GFX_ERROR(
					"AppendBuffer::At",
					"Invalid Virtual Index");
			}
			return m_data[physicalIndex];
		}

		[[nodiscard]] T&
		At(uint32_t virtualIndex)
		{
			if (virtualIndex >= m_id2Idx.size())
			{
				THROW_GFX_ERROR(
					"AppendBuffer::At",
					"Invalid Virtual Index");
			}
			uint32_t physicalIndex = m_id2Idx[virtualIndex];
			if (physicalIndex == INVALID_PHYSICAL_INDEX || physicalIndex >= m_data.size())
			{
				THROW_GFX_ERROR(
					"AppendBuffer::At",
					"Invalid Virtual Index");
			}
			m_dirty = true;  // Assume mutation
			return m_data[physicalIndex];
		}

		// Access CPU-local extra data
		[[nodiscard]] const ExtraData&
		GetExtraData(uint32_t virtualIndex) const
			requires(!std::is_same_v<ExtraData, NoExtraData>)
		{
			if (virtualIndex >= m_id2Idx.size())
			{
				THROW_GFX_ERROR(
					"AppendBuffer::GetExtraData",
					"Invalid Virtual Index");
			}
			uint32_t physicalIndex = m_id2Idx[virtualIndex];
			if (physicalIndex == INVALID_PHYSICAL_INDEX || physicalIndex >= m_extraData.size())
			{
				THROW_GFX_ERROR(
					"AppendBuffer::GetExtraData",
					"Invalid Virtual Index");
			}
			return m_extraData[physicalIndex];
		}

		[[nodiscard]] ExtraData&
		GetExtraData(uint32_t virtualIndex)
			requires(!std::is_same_v<ExtraData, NoExtraData>)
		{
			if (virtualIndex >= m_id2Idx.size())
			{
				THROW_GFX_ERROR(
					"AppendBuffer::GetExtraData",
					"Invalid Virtual Index");
			}
			uint32_t physicalIndex = m_id2Idx[virtualIndex];
			if (physicalIndex == INVALID_PHYSICAL_INDEX || physicalIndex >= m_extraData.size())
			{
				THROW_GFX_ERROR(
					"AppendBuffer::GetExtraData",
					"Invalid Virtual Index");
			}
			return m_extraData[physicalIndex];
		}

		[[nodiscard]] bool
		IsValid(uint32_t virtualIndex) const noexcept
		{
			return virtualIndex < m_id2Idx.size() &&
			       m_id2Idx[virtualIndex] != INVALID_PHYSICAL_INDEX &&
			       m_id2Idx[virtualIndex] < m_data.size();
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

		[[nodiscard]]
		bool
		Update(nvrhi::CommandListHandle cmdList, nvrhi::DeviceHandle device)
		{
			if (!m_dirty)
				return false;

			const size_t requiredDataSize = m_data.size() * sizeof(T);
			const size_t requiredV2PSize  = m_id2Idx.size() * sizeof(uint32_t);
			bool         recreated        = false;

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
			// Only upload GPU data (T), not ExtraData
			cmdList->writeBuffer(m_buffer, m_data.data(), requiredDataSize);

			if (m_useRedirectTableOnGPU)
			{
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
				cmdList->writeBuffer(m_redirectTable, m_id2Idx.data(), requiredV2PSize);
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

		void
		Clear()
		{
			m_data.clear();
			m_data.emplace_back(T{});

			if constexpr (!std::is_same_v<ExtraData, NoExtraData>)
			{
				m_extraData.clear();
				m_extraData.emplace_back(ExtraData{});
			}

			m_id2Idx.clear();
			m_id2Idx.emplace_back(0);
			m_idx2Id.clear();
			m_idx2Id.emplace_back(0);
			m_dirty = true;
		}

	private:
		// TODO: Use a more optimized system using bit flag vector with each flag representing 512 indices
		// OS-style first-fit allocation
		uint32_t
		AllocateVirtualIndex()
		{
			for (uint32_t i = 1; i < m_id2Idx.size(); ++i)
			{
				if (m_id2Idx[i] == INVALID_PHYSICAL_INDEX)
				{
					return i;
				}
			}
			return static_cast<uint32_t>(m_id2Idx.size());
		}

		std::vector<T>         m_data{};
		std::vector<ExtraData> m_extraData{};
		std::vector<uint32_t>  m_id2Idx{};
		std::vector<uint32_t>  m_idx2Id{};
		nvrhi::BufferHandle    m_buffer{};
		nvrhi::BufferHandle    m_redirectTable{};
		bool                   m_dirty                 = false;
		bool                   m_useRedirectTableOnGPU = true;
	};

	template <typename T, typename ExtraData = NoExtraData>
	using AppendBufferView = AppendBuffer<T, ExtraData>::View;
}
