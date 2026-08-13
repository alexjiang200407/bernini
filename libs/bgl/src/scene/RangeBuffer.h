#pragma once
#include "scene/GrowableGpuBuffer.h"
#include "uniforms/DescriptorHandle.h"
#include <core/containers/multi_slot_vector.h>
#include <core/err/util.h>

namespace bgl
{
	struct RangeBufferDesc
	{
		// Where the arena starts, not where it ends: it grows on demand and is bounded only by
		// device memory. The reserved null element is carried on top of this, so a caller's budget
		// is entirely its own.
		uint32_t    initialCount = 0;
		uint32_t    blockSize    = 65536;  // Default to the sweet spot 64KB
		std::string debugName;
	};

	template <typename T>
	concept RangeBufferConcept =
		core::MultiSlotElementConcept<T> && core::type_traits::trivially_copyable<T>;

	/**
	 * A GPU-mirrored buffer of variable-length ranges of trivially-copyable
	 * elements.
	 *
	 * Element 0 is reserved and never allocated, so the start index in every live
	 * `idl::Range` is distinguishable from a null one.
	 */
	template <RangeBufferConcept T, typename Meta = void>
	class RangeBuffer
	{
	public:
	private:
		static constexpr bool c_HasMeta = !std::is_void_v<Meta>;
		using MetaElem                  = std::conditional_t<c_HasMeta, Meta, int>;
		using MetaStorage = std::conditional_t<c_HasMeta, std::vector<MetaElem>, std::monostate>;

	public:
		RangeBuffer() noexcept = default;
		RangeBuffer(RangeBufferDesc desc, ResourceManagerRef resourceManager)
		{
			Init(std::move(desc), std::move(resourceManager));
		}

		RangeBuffer(const RangeBuffer&)     = delete;
		RangeBuffer(RangeBuffer&&) noexcept = default;

		RangeBuffer&
		operator=(const RangeBuffer&) = delete;

		RangeBuffer&
		operator=(RangeBuffer&&) noexcept = default;

		void
		Init(RangeBufferDesc desc, ResourceManagerRef resourceManager)
		{
			gassert(desc.initialCount > 0, "RangeBuffer must have a positive initial count");
			gassert(desc.blockSize > 0, "Block size must be greater than zero");
			gassert(resourceManager != nullptr, "RangeBuffer requires a valid ResourceManager");

			m_Desc = std::move(desc);

			const uint32_t capacity = m_Desc.initialCount + 1;

			m_Storage
				.Init(std::move(resourceManager), m_Desc.debugName, sizeof(T), capacity, false);

			m_Data.reset(capacity);

			if constexpr (c_HasMeta)
			{
				m_Metadata.assign(capacity, Meta{});
			}

			ResizeDirtyBlocks(capacity);
			m_HasAnyDirtyBlocks = false;

			ReserveNullRange();
		}

		// True once Init() has created the GPU buffer and before Release().
		[[nodiscard]] bool
		IsInitialized() const noexcept
		{
			return m_Storage.IsInitialized();
		}

		[[nodiscard]] uint32_t
		Capacity() const noexcept
		{
			return m_Storage.GetCapacity();
		}

		core::multi_slot_handle
		Add(std::span<const T> elem)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(
				elem.size() < std::numeric_limits<uint32_t>::max(),
				"Element count exceeds uint32_t limits");

			// AllocateRange already marked this exact range dirty, and nothing can flush between
			// there and here, so the write below is covered without marking it a second time.
			auto handle = AllocateRange(static_cast<uint32_t>(elem.size()));

			// One copy, not per-element writes: operator[] validates every slot, and a large
			// mesh's vertex stream is millions of elements. The range was allocated contiguous
			// and live just above, so there is nothing left to validate.
			std::memcpy(
				static_cast<T*>(m_Data.data()) + handle.index,
				elem.data(),
				elem.size() * sizeof(T));

			return handle;
		}

		// Re-read every frame: growth mints a new handle and retires the old one (see
		// GrowableGpuBuffer), so a cached descriptor index goes stale.
		[[nodiscard]]
		DescriptorHandle
		GetDescriptorHandle() const noexcept
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			return DescriptorHandle(m_Storage.GetHandle().bindlessIndex);
		}

		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			return m_Storage.GetHandle();
		}

		[[nodiscard]] core::multi_slot_handle
		AllocateRange(uint32_t count)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(count > 0, "AllocateRange requires a positive count");

			auto handle = TryAllocateSlots(count);
			if (handle.is_null())
			{
				Grow(count);
				handle = TryAllocateSlots(count);

				if (handle.is_null())
				{
					core::throw_runtime_error(
						"RangeBuffer '{}': {} slots do not fit even after growing to {}",
						m_Desc.debugName,
						count,
						Capacity());
				}
			}

			if constexpr (c_HasMeta)
			{
				m_Metadata[handle.index] = Meta{};
			}

			MarkRangeDirty(handle.index, handle.count);

			return handle;
		}

		// A handle is valid only while its range is live and its generation
		// matches; once Erase'd the stale handle reports invalid, catching
		// use-after-free on the CPU side.
		[[nodiscard]] bool
		IsValid(core::multi_slot_handle handle) const noexcept
		{
			return m_Data.valid(handle.index, handle.generation);
		}

		// Reports whether a live range starts at `rootIndex`, used when only the
		// GPU-side index is known (db structs store indices, not generations). The reserved null
		// element is allocated but belongs to no caller, so a null offset answers false.
		[[nodiscard]] bool
		IsIndexValid(uint32_t rootIndex) const noexcept
		{
			return rootIndex != 0 && m_Data.is_allocated_root(rootIndex);
		}

		void
		Set(core::multi_slot_handle handle, uint32_t relativeIndex, T value)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(
				relativeIndex < handle.count,
				"Relative index exceeds allocated range count bounds");

			uint32_t physicalIndex = handle.index + relativeIndex;
			gassert(
				m_Data.valid(physicalIndex),
				"Attempting to access an unallocated or erased element slot");

			// Mark only the specific element slot dirty
			MarkRangeDirty(physicalIndex, 1);
			m_Data[physicalIndex] = std::move(value);
		}

		// Overwrites a single element by its absolute slot index (the GPU-side index), used when
		// only that index is known -- e.g. mutating one submesh of a shared geom by its global
		// position. The slot must be live.
		void
		SetAtIndex(uint32_t index, T value)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(m_Data.valid(index), "SetAtIndex on an inactive element slot");
			MarkRangeDirty(index, 1);
			m_Data[index] = std::move(value);
		}

		void
		Erase(core::multi_slot_handle handle)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			MarkRangeDirty(handle.index, handle.count);
			m_Data.erase(handle);
		}

		// Erases the range starting at `rootIndex`, used when only the GPU-side
		// index is known (db structs store indices, not generations). A live
		// allocation must start at that index.
		void
		EraseByIndex(uint32_t rootIndex)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(
				m_Data.valid(rootIndex, m_Data.generation(rootIndex)),
				"EraseByIndex on an index with no live range");
			Erase(m_Data.handle_at(rootIndex));
		}

		template <typename M = Meta>
		[[nodiscard]] M&
		MetaAt(uint32_t rootIndex) noexcept
			requires(!std::is_void_v<M>)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(
				m_Data.valid(rootIndex, m_Data.generation(rootIndex)),
				"MetaAt on an index with no live range");
			return m_Metadata[rootIndex];
		}

		template <typename M = Meta>
		[[nodiscard]] const M&
		MetaAt(uint32_t rootIndex) const noexcept
			requires(!std::is_void_v<M>)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(
				m_Data.valid(rootIndex, m_Data.generation(rootIndex)),
				"MetaAt on an index with no live range");
			return m_Metadata[rootIndex];
		}

		[[nodiscard]] const T&
		Get(core::multi_slot_handle handle, uint32_t relativeIndex) const
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(
				relativeIndex < handle.count,
				"Relative index exceeds allocated range count bounds");
			uint32_t physicalIndex = handle.index + relativeIndex;
			return m_Data[physicalIndex];
		}

		[[nodiscard]] const T&
		AtIndex(uint32_t index) const
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(m_Data.valid(index), "AtIndex on an inactive element slot");
			return m_Data[index];
		}

		void
		Update(ICommandList* cmdList)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(cmdList != nullptr, "Update requires a valid ICommandList");
			gassert(cmdList->IsOpen(), "ICommandList must be open to update RangeBuffer");

			// Before the dirty regions, never after: the forward copy would overwrite them.
			m_Storage.FlushGrowth(cmdList);

			if (!m_HasAnyDirtyBlocks)
				return;

			const uint32_t totalBytes = static_cast<uint32_t>(m_Data.size() * sizeof(T));

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

		[[nodiscard]] const std::vector<bool>&
		GetDirtyBlocks() const noexcept
		{
			return m_DirtyBlocks;
		}

		void
		Release(bool deferred = true) noexcept
		{
			if (IsInitialized())
			{
				m_Storage.Release(deferred);
				m_Data.clear();
				m_DirtyBlocks.clear();
				if constexpr (c_HasMeta)
				{
					m_Metadata.clear();
				}
				m_HasAnyDirtyBlocks = false;
			}
		}

	private:
		// Held for the buffer's lifetime so no caller is handed the offset that means null. Marked
		// dirty so the GPU sees a zeroed element there rather than whatever the allocation held.
		void
		ReserveNullRange()
		{
			const core::multi_slot_handle handle = m_Data.allocate_slots(1);
			gassert(handle.index == 0, "The null range must own the first element");
			MarkRangeDirty(handle.index, handle.count);
		}

		[[nodiscard]] core::multi_slot_handle
		TryAllocateSlots(uint32_t count) noexcept
		{
			try
			{
				return m_Data.allocate_slots(count);
			}
			catch (const std::runtime_error&)
			{
				return {};
			}
		}

		// Raises the ceiling far enough that `count` contiguous slots fit above the live set even
		// if every existing free segment is too fragmented to serve them.
		void
		Grow(uint32_t count)
		{
			const uint32_t grown = NextGpuBufferCapacity(Capacity(), Capacity() + count, sizeof(T));

			// GPU side first: it is the one that can fail, and it leaves nothing behind when it
			// does, so the mirror and the buffer cannot end up disagreeing on capacity.
			m_Storage.Grow(grown);
			m_Data.grow(grown);

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
		MarkRangeDirty(uint32_t startIdx, uint32_t count)
		{
			if (count == 0)
				return;

			const uint32_t startOffsetBytes = startIdx * sizeof(T);
			const uint32_t endOffsetBytes   = ((startIdx + count) * sizeof(T)) - 1;

			const uint32_t startBlock = startOffsetBytes / m_Desc.blockSize;
			const uint32_t endBlock   = endOffsetBytes / m_Desc.blockSize;

			gassert(
				endBlock < m_DirtyBlocks.size(),
				"Dirty tracking index spans out of block limits");

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
				cmdList->WriteBufferSlice(m_Storage.GetHandle(), m_Data.data(), offset, size);
			}
		}

	private:
		RangeBufferDesc   m_Desc;
		GrowableGpuBuffer m_Storage;

		MetaStorage m_Metadata;

		std::vector<bool>          m_DirtyBlocks;
		core::multi_slot_vector<T> m_Data;
		bool                       m_HasAnyDirtyBlocks = false;
	};

	template <typename T>
	struct is_range_buffer : std::false_type
	{};

	template <typename... Args>
	struct is_range_buffer<RangeBuffer<Args...>> : std::true_type
	{};

	template <typename T>
	inline constexpr bool is_range_buffer_v = is_range_buffer<std::decay_t<T>>::value;
}
