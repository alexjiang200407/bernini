#pragma once
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "scene/GrowableGpuBuffer.h"
#include "uniforms/DescriptorHandle.h"
#include <core/containers/slot_vector.h>
#include <core/type_traits.h>

namespace bgl
{
	struct EntryBufferDesc
	{
		// Where the arena starts, not where it ends: it grows on demand and is bounded only by
		// device memory.
		uint32_t    initialCount = 0;
		uint32_t    blockSize    = 65536;
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
	private:
		static constexpr bool c_HasMeta = !std::is_void_v<Meta>;
		using MetaElem                  = std::conditional_t<c_HasMeta, Meta, int>;
		using MetaStorage = std::conditional_t<c_HasMeta, std::vector<MetaElem>, std::monostate>;

	public:
		EntryBuffer() noexcept = default;
		EntryBuffer(EntryBufferDesc desc, ResourceManagerRef resourceManager)
		{
			Init(std::move(desc), std::move(resourceManager));
		}

		EntryBuffer(const EntryBuffer&)     = delete;
		EntryBuffer(EntryBuffer&&) noexcept = default;

		EntryBuffer&
		operator=(const EntryBuffer&) = delete;

		EntryBuffer&
		operator=(EntryBuffer&&) noexcept = default;

		void
		Init(EntryBufferDesc desc, ResourceManagerRef resourceManager)
		{
			gassert(desc.initialCount > 0, "EntryBuffer must have a positive initial count");
			gassert(desc.blockSize > 0, "Block size must be greater than zero");
			gassert(resourceManager != nullptr, "EntryBuffer requires a valid ResourceManager");

			m_Desc = std::move(desc);

			m_Storage.Init(
				std::move(resourceManager),
				m_Desc.debugName,
				sizeof(T),
				m_Desc.initialCount,
				false);

			m_Entries.reset(m_Desc.initialCount);

			if constexpr (c_HasMeta)
			{
				m_Metadata.assign(m_Desc.initialCount, Meta{});
			}

			ResizeDirtyBlocks(m_Desc.initialCount);
			m_HasAnyDirtyBlocks = false;
		}

		// True once Init() has created the GPU buffer and before Release().
		[[nodiscard]] bool
		IsInitialized() const noexcept
		{
			return m_Storage.IsInitialized();
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

		[[nodiscard]] uint32_t
		Capacity() const noexcept
		{
			return m_Storage.GetCapacity();
		}

		template <typename... Args>
		core::slot_handle
		EmplaceBack(Args&&... args)
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");

			auto slot = m_Entries.try_allocate_and_emplace(std::forward<Args>(args)...);
			if (slot.is_null())
			{
				Grow();
				slot = m_Entries.try_allocate_and_emplace(std::forward<Args>(args)...);

				if (slot.is_null())
				{
					core::throw_runtime_error(
						"EntryBuffer '{}': no free slot even after growing to {}",
						m_Desc.debugName,
						Capacity());
				}
			}

			ResetMeta(slot.index);
			MarkDirty(slot.index);
			return slot;
		}

		core::slot_handle
		Add(T value)
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			auto slot = EmplaceBack();
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

			// Before the dirty regions, never after: the forward copy would overwrite them.
			m_Storage.FlushGrowth(cmdList);

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

		// Re-read every frame: growth mints a new handle and retires the old one (see
		// GrowableGpuBuffer), so a cached descriptor index goes stale.
		DescriptorHandle
		GetDescriptorHandle() const noexcept
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			return DescriptorHandle(m_Storage.GetHandle().slot);
		}

		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
			gassert(IsInitialized(), "EntryBuffer is uninitialized; call Init() first");
			return m_Storage.GetHandle();
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
		Release(bool deferred = true) noexcept
		{
			if (IsInitialized())
			{
				m_Storage.Release(deferred);
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
		Grow()
		{
			const uint32_t grown = NextGpuBufferCapacity(Capacity(), Capacity() + 1, sizeof(T));

			// GPU side first: it is the one that can fail, and it leaves nothing behind when it
			// does, so the mirror and the buffer cannot end up disagreeing on capacity.
			m_Storage.Grow(grown);
			m_Entries.grow(grown);

			if constexpr (c_HasMeta)
			{
				m_Metadata.resize(grown, Meta{});
			}

			ResizeDirtyBlocks(grown);
		}

		void
		ResizeDirtyBlocks(uint32_t capacity)
		{
			const uint64_t totalBytes = static_cast<uint64_t>(capacity) * sizeof(T);
			const auto     numBlocks =
				static_cast<size_t>((totalBytes + m_Desc.blockSize - 1) / m_Desc.blockSize);

			m_DirtyBlocks.resize(numBlocks, false);
		}

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
			gassert(IsInitialized(), "EntryBuffer storage cannot be null");

			const uint32_t offset = startBlk * m_Desc.blockSize;
			uint32_t       size   = (endBlk - startBlk) * m_Desc.blockSize;

			if (offset >= totalBytes)
			{
				return;
			}

			if (offset + size > totalBytes)
			{
				size = totalBytes - offset;
			}

			if (size > 0)
			{
				cmdList->WriteBufferSlice(m_Storage.GetHandle(), m_Entries.data(), offset, size);
			}
		}

	private:
		EntryBufferDesc      m_Desc;
		GrowableGpuBuffer    m_Storage;
		core::slot_vector<T> m_Entries;

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
