#pragma once
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "uniforms/DescriptorHandle.h"
#include <core/containers/slot_vector.h>
#include <core/type_traits.h>

namespace bgl
{
	struct EntryBufferDesc
	{
		uint32_t    maxCount  = 0;
		uint32_t    blockSize = 65536;
		std::string debugName;
	};

	template <typename T>
	concept EntryBufferConcept =
		core::SlotElementConcept<T> && core::type_traits::trivially_copyable<T>;

	/**
	 * A GPU-mirrored slot buffer of trivially-copyable elements.
	 */
	template <EntryBufferConcept T, typename Meta = void>
	class EntryBuffer
	{
	public:
		struct CopyRange
		{
			uint32_t offsetBytes = 0;
			uint32_t sizeBytes   = 0;
		};

	private:
		static constexpr bool c_HasMeta = !std::is_void_v<Meta>;
		using MetaElem                  = std::conditional_t<c_HasMeta, Meta, int>;
		using MetaStorage = std::conditional_t<c_HasMeta, std::vector<MetaElem>, std::monostate>;

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

			if constexpr (c_HasMeta)
			{
				m_Metadata.assign(m_Desc.maxCount, Meta{});
			}

			const uint32_t totalBytes = m_Desc.maxCount * sizeof(T);
			const uint32_t numBlocks  = (totalBytes + m_Desc.blockSize - 1) / m_Desc.blockSize;

			m_DirtyBlocks.assign(numBlocks, false);
			m_HasAnyDirtyBlocks = false;

			{
				StructBufferDesc bufDesc;
				bufDesc.debugName    = m_Desc.debugName;
				bufDesc.elementCount = m_Desc.maxCount;
				bufDesc.stride       = sizeof(T);
				bufDesc.isUav        = false;

				m_BufferHandle = m_ResourceManager->CreateStructBuffer(bufDesc);
			}
		}

		// True once Init() has created the GPU buffer and before Release().
		[[nodiscard]] bool
		IsInitialized() const noexcept
		{
			return !m_BufferHandle.IsNull();
		}

		[[nodiscard]] bool
		IsValid(core::slot_handle handle) const noexcept
		{
			return m_Entries.valid(handle.index, handle.generation);
		}

		[[nodiscard]] bool
		IsIndexValid(uint32_t index) const noexcept
		{
			return m_Entries.allocated(index);
		}

		template <typename... Args>
		core::slot_handle
		EmplaceBack(Args&&... args)
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");

			try
			{
				auto slot = m_Entries.allocate_and_emplace(std::forward<Args>(args)...);
				ResetMeta(slot.index);
				MarkDirty(slot.index);
				return slot;
			}
			catch (const std::runtime_error& e)
			{
				core::throw_runtime_error(
					"EntryBuffer '{}' allocation failed: {}",
					m_Desc.debugName,
					e.what());
			}
		}

		core::slot_handle
		Add(T value)
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			auto slot = m_Entries.allocate_slot();
			ResetMeta(slot.index);
			Set(slot, std::move(value));
			return slot;
		}

		void
		Set(core::slot_handle slot, T value) noexcept
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			gassert(m_Entries.valid(slot.index, slot.generation), "Invalid slot handle");
			MarkDirty(slot.index);
			m_Entries[slot.index] = std::move(value);
		}

		const T&
		operator[](core::slot_handle slot) const noexcept
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			gassert(m_Entries.valid(slot.index, slot.generation), "Invalid slot handle");
			return m_Entries[slot.index];
		}

		const T&
		AtIndex(uint32_t index) const noexcept
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			gassert(m_Entries.allocated(index), "AtIndex on an unallocated slot");
			return m_Entries[index];
		}

		void
		Erase(core::slot_handle slot) noexcept
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			gassert(m_Entries.valid(slot.index, slot.generation), "Invalid slot handle");
			m_Entries.release_slot(slot.index);
		}

		void
		EraseByIndex(uint32_t index) noexcept
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			gassert(m_Entries.allocated(index), "EraseByIndex on an unallocated slot");
			m_Entries.release_slot(index);
		}

		template <typename M = Meta>
		[[nodiscard]] M&
		MetaAt(uint32_t index) noexcept
			requires(!std::is_void_v<M>)
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			gassert(m_Entries.allocated(index), "MetaAt on an unallocated slot");
			return m_Metadata[index];
		}

		template <typename M = Meta>
		[[nodiscard]] const M&
		MetaAt(uint32_t index) const noexcept
			requires(!std::is_void_v<M>)
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			gassert(m_Entries.allocated(index), "MetaAt on an unallocated slot");
			return m_Metadata[index];
		}

		void
		Update(ICommandList* cmdList) noexcept
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
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
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			return DescriptorHandle(m_BufferHandle.idx);
		}

		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			return m_BufferHandle;
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

		void
		Release(uint64_t fenceValue, bool deferred = true) noexcept
		{
			if (!m_BufferHandle.IsNull())
			{
				m_ResourceManager->DestroyBuffer(m_BufferHandle, fenceValue, deferred);
				m_BufferHandle = {};
				m_Entries.clear();
				m_DirtyBlocks.clear();
				if constexpr (c_HasMeta)
				{
					m_Metadata.clear();
				}
				m_HasAnyDirtyBlocks = false;
			}
		}

	private:
		void
		ResetMeta(uint32_t index) noexcept
		{
			if constexpr (c_HasMeta)
			{
				m_Metadata[index] = Meta{};
			}
		}

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
		IssueCopy(
			ICommandList* cmdList,
			uint32_t      startBlk,
			uint32_t      endBlk,
			uint32_t      totalBytes) noexcept
		{
			gassert(!m_BufferHandle.IsNull(), "m_BufferHandle cannot be null");

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

	private:
		EntryBufferDesc       m_Desc;
		ResourceManagerHandle m_ResourceManager;
		BufferHandle          m_BufferHandle;
		core::slot_vector<T>  m_Entries;

		MetaStorage m_Metadata;

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
