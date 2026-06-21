#pragma once
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "types/CpuAccessMode.h"
#include "uniforms/DescriptorHandle.h"
#include <core/containers/slot_vector.h>
#include <core/type_traits.h>

namespace bgl
{
	struct EntryBufferDesc
	{
		uint32_t      maxCount  = 0;
		CpuAccessMode cpuAccess = CpuAccessMode::kDefault;
		uint32_t      blockSize = 65536;
		std::string   debugName;
	};

	enum class EntryBufferState
	{
		kUpdate,
		kShader,
		kNone,
	};

	template <typename T>
	concept EntryBufferConcept =
		core::SlotElementConcept<T> && core::type_traits::trivially_copyable<T>;

	template <EntryBufferConcept T>
	class EntryBuffer
	{
	public:
		struct CopyRange
		{
			uint32_t offsetBytes = 0;
			uint32_t sizeBytes   = 0;
		};

	public:
		EntryBuffer() noexcept = default;
		EntryBuffer(EntryBufferDesc desc, ResourceManagerHandle resourceManager) noexcept
		{
			Init(std::move(desc), std::move(resourceManager));
		}

		void
		Init(EntryBufferDesc desc, ResourceManagerHandle resourceManager) noexcept
		{
			gassert(desc.maxCount > 0, "EntryBuffer must have a positive maxCount");
			gassert(desc.blockSize > 0, "Block size must be greater than zero");
			gassert(resourceManager != nullptr, "EntryBuffer requires a valid ResourceManager");

			m_Desc            = std::move(desc);
			m_ResourceManager = std::move(resourceManager);

			m_Entries.reset(m_Desc.maxCount);

			const uint32_t totalBytes = m_Desc.maxCount * sizeof(T);
			const uint32_t numBlocks  = (totalBytes + m_Desc.blockSize - 1) / m_Desc.blockSize;

			m_DirtyBlocks.assign(numBlocks, false);
			m_HasAnyDirtyBlocks = false;

			{
				BufferDesc bufDesc;
				bufDesc.debugName    = m_Desc.debugName;
				bufDesc.cpuAccess    = m_Desc.cpuAccess;
				bufDesc.elementCount = m_Desc.maxCount;
				bufDesc.stride       = sizeof(T);
				bufDesc.isUav        = false;

				m_BufferHandle = m_ResourceManager->CreateStructBuffer(bufDesc);
			}
		}

		template <typename... Args>
		core::slot_handle
		EmplaceBack(Args&&... args)
		{
			auto slot = m_Entries.allocate_and_emplace(std::forward<Args>(args)...);
			MarkDirty(slot.index);
			return slot;
		}

		core::slot_handle
		Add(T value)
		{
			auto slot = m_Entries.allocate_slot();
			Set(slot, std::move(value));
			return slot;
		}

		void
		Set(core::slot_handle slot, T value)
		{
			gassert(m_Entries.valid(slot.index, slot.generation), "Invalid slot handle");
			MarkDirty(slot.index);
			m_Entries[slot.index] = std::move(value);
		}

		const T&
		operator[](core::slot_handle slot) const
		{
			gassert(m_Entries.valid(slot.index, slot.generation), "Invalid slot handle");
			return m_Entries[slot.index];
		}

		void
		Erase(core::slot_handle slot)
		{
			gassert(m_Entries.valid(slot.index, slot.generation), "Invalid slot handle");
			m_Entries.release_slot(slot.index);
		}

		void
		Transition(ICommandList* cmdList, EntryBufferState prevState, EntryBufferState newState)
		{
			BufferBarrierDesc barrier = {};

			auto prevAccessSync = GetBarrierAccessSync(prevState);
			auto newAccessSync  = GetBarrierAccessSync(newState);

			barrier.accessBefore = prevAccessSync.first;
			barrier.syncBefore   = prevAccessSync.second;
			barrier.accessAfter  = newAccessSync.first;
			barrier.syncAfter    = newAccessSync.second;

			cmdList->Barrier(m_BufferHandle, barrier);
		}

		void
		Update(ICommandList* cmdList)
		{
			gassert(cmdList != nullptr, "Update requires a valid ICommandList");
			gassert(cmdList->IsOpen(), "ICommandList must be open to update EntryBuffer");

			if (!m_HasAnyDirtyBlocks)
				return;

			const uint32_t totalBytes = static_cast<uint32_t>(m_Entries.size() * sizeof(T));

			bool     inRange    = false;
			uint32_t startBlock = 0;

			for (size_t i = 0; i < m_DirtyBlocks.size(); ++i)
			{
				if (m_DirtyBlocks[i])
				{
					if (!inRange)
					{
						startBlock = static_cast<uint32_t>(i);
						inRange    = true;
					}
				}
				else
				{
					if (inRange)
					{
						IssueCopy(cmdList, startBlock, static_cast<uint32_t>(i), totalBytes);
						inRange = false;
					}
				}
			}

			if (inRange)
			{
				IssueCopy(
					cmdList,
					startBlock,
					static_cast<uint32_t>(m_DirtyBlocks.size()),
					totalBytes);
			}

			std::fill(m_DirtyBlocks.begin(), m_DirtyBlocks.end(), false);
			m_HasAnyDirtyBlocks = false;
		}

		DescriptorHandle
		GetDescriptorHandle() const noexcept
		{
			return DescriptorHandle(m_BufferHandle.idx);
		}

		[[nodiscard]] bool
		IsBlockDirty(uint32_t blockIdx) const
		{
			return blockIdx < m_DirtyBlocks.size() && m_DirtyBlocks[blockIdx];
		}

		[[nodiscard]] uint32_t
		CountDirtyBlocks() const
		{
			return static_cast<uint32_t>(
				std::count(m_DirtyBlocks.begin(), m_DirtyBlocks.end(), true));
		}

	private:
		void
		MarkDirty(uint32_t index)
		{
			const uint32_t elementOffsetBytes = index * sizeof(T);

			const uint32_t startBlock = elementOffsetBytes / m_Desc.blockSize;
			const uint32_t endBlock   = (elementOffsetBytes + sizeof(T) - 1) / m_Desc.blockSize;

			gassert(endBlock < m_DirtyBlocks.size(), "Dirty tracking index out of block bounds");

			for (uint32_t block = startBlock; block <= endBlock; ++block)
			{
				m_DirtyBlocks[block] = true;
			}
			m_HasAnyDirtyBlocks = true;
		}

		void
		IssueCopy(ICommandList* cmdList, uint32_t startBlk, uint32_t endBlk, uint32_t totalBytes)
		{
			const uint32_t offset = startBlk * m_Desc.blockSize;
			uint32_t       size   = (endBlk - startBlk) * m_Desc.blockSize;

			if (offset + size > totalBytes)
			{
				size = totalBytes - offset;
			}

			if (size > 0)
			{
				cmdList->WriteBuffer(m_BufferHandle, m_Entries.data(), offset, size);
			}
		}

		std::pair<BarrierAccess, BarrierSync>
		GetBarrierAccessSync(EntryBufferState state)
		{
			switch (state)
			{
			case EntryBufferState::kUpdate:
				return { BarrierAccessFlag::kCopyDest, BarrierSyncFlag::kCopy };
			case EntryBufferState::kShader:
				return { BarrierAccessFlag::kShaderResource, BarrierSyncFlag::kVertexShader };
			case EntryBufferState::kNone:
				return { BarrierAccessFlag::kNone, BarrierSyncFlag::kNone };
			default:
				gfatal("Invalid EntryBufferState");
			}
		}

	private:
		EntryBufferDesc       m_Desc;
		ResourceManagerHandle m_ResourceManager;
		BufferHandle          m_BufferHandle;
		core::slot_vector<T>  m_Entries;

		std::vector<bool> m_DirtyBlocks;
		bool              m_HasAnyDirtyBlocks = false;
	};

	template <typename T>
	struct is_entry_buffer : std::false_type
	{};

	template <typename... Args>
	struct is_entry_buffer<EntryBuffer<Args...>> : std::true_type
	{};

	template <typename T>
	inline constexpr bool is_entry_buffer_v = is_entry_buffer<std::decay_t<T>>::value;
}
