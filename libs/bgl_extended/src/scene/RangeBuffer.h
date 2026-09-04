#pragma once
#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"
#include "scene/GrowableGpuBuffer.h"
#include "uniforms/DescriptorHandle.h"
#include <algorithm>
#include <bgl_common/gassert.h>
#include <core/containers/multi_slot_handle.h>
#include <core/containers/multi_slot_vector.h>
#include <core/err/util.h>
#include <core/type_traits.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace bgl
{
	struct RangeBufferDesc
	{
		// Where the arena starts, not where it ends: it grows on demand and is bounded only by
		// device memory, or by maxBytes. The reserved null element is carried on top of this, so a
		// caller's budget is entirely its own.
		uint32_t initialCount = 0;
		uint32_t blockSize    = 65536;  // Default to the sweet spot 64KB

		// 0 leaves growth bounded only by device memory. A buffer read through a raw view sets it
		// to c_MaxRawBufferBytes, since a byte past that is unaddressable however much of it the
		// device allocated -- growth that would cross it throws rather than handing out an offset
		// no shader can reach.
		uint64_t maxBytes = 0;

		// Creates the storage with a raw (ByteAddressBuffer) view rather than a structured one. The
		// shader wrapper must match: see docs/rhi.md.
		bool isRaw = false;

		std::string debugName;
	};

	template <typename T>
	concept RangeBufferConcept =
		core::MultiSlotElementConcept<T> && core::type_traits::trivially_copyable<T>;

	// The first and last dirty block a range of elements touches, both inclusive.
	struct DirtyBlockSpan
	{
		uint32_t first = 0;
		uint32_t last  = 0;

		bool
		operator==(const DirtyBlockSpan&) const noexcept = default;
	};

	// The byte window one run of dirty blocks uploads, clamped to what the mirror actually holds.
	struct CopySlice
	{
		uint64_t offset = 0;
		uint64_t size   = 0;

		bool
		operator==(const CopySlice&) const noexcept = default;
	};

	/**
	 * The bytes to upload for dirty blocks `[startBlk, endBlk)`.
	 *
	 * A free function for the same reason as FindDirtyBlocks, and it is the arithmetic that was
	 * actually 32-bit: a fully dirty arena at its byte ceiling is 65536 blocks of 65536 bytes,
	 * whose product is 2^32 -- zero in 32 bits, so the copy was skipped and nothing uploaded.
	 *
	 * @post a slice of zero size where the run starts past the end of the mirror.
	 */
	[[nodiscard]] constexpr CopySlice
	MakeCopySlice(
		uint32_t startBlk,
		uint32_t endBlk,
		uint32_t blockSize,
		uint64_t totalBytes) noexcept
	{
		const uint64_t offset = static_cast<uint64_t>(startBlk) * blockSize;
		if (offset >= totalBytes)
		{
			return {};
		}

		const uint64_t size = static_cast<uint64_t>(endBlk - startBlk) * blockSize;
		return { offset, std::min(size, totalBytes - offset) };
	}

	/**
	 * The capacity a growth of `count` more elements should take a buffer to, clamped to a byte
	 * ceiling (`maxBytes` of 0 leaves it bounded only by device memory).
	 *
	 * A free function so the ceiling can be tested at the real 2^32 without allocating it.
	 *
	 * @post 0 when the request itself crosses the ceiling -- the caller has the name and the numbers
	 * to report it with.
	 */
	[[nodiscard]] inline uint32_t
	GrowCapacityFor(
		uint32_t current,
		uint32_t count,
		uint64_t elementSize,
		uint64_t maxBytes) noexcept
	{
		const uint64_t requiredBytes = (static_cast<uint64_t>(current) + count) * elementSize;

		if (maxBytes != 0 && requiredBytes > maxBytes)
		{
			return 0;
		}

		const uint32_t grown =
			NextGpuBufferCapacity(current, current + count, static_cast<uint32_t>(elementSize));

		// The growth curve overshoots on purpose; the ceiling is not a budget to overshoot past.
		return maxBytes == 0 ?
		           grown :
		           static_cast<uint32_t>(std::min<uint64_t>(grown, maxBytes / elementSize));
	}

	/**
	 * Which blocks a range of `count` elements starting at `startIdx` lands in.
	 *
	 * A free function beside the two above it, so an arena at its byte ceiling can be reasoned
	 * about without allocating one.
	 *
	 * @pre count is non-zero and blockSize is non-zero.
	 */
	[[nodiscard]] constexpr DirtyBlockSpan
	FindDirtyBlocks(
		uint32_t startIdx,
		uint32_t count,
		uint64_t elementSize,
		uint32_t blockSize) noexcept
	{
		const uint64_t startOffsetBytes = static_cast<uint64_t>(startIdx) * elementSize;
		const uint64_t endOffsetBytes =
			((static_cast<uint64_t>(startIdx) + count) * elementSize) - 1;

		return { static_cast<uint32_t>(startOffsetBytes / blockSize),
			     static_cast<uint32_t>(endOffsetBytes / blockSize) };
	}

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

			if (m_Desc.maxBytes != 0 &&
			    static_cast<uint64_t>(capacity) * sizeof(T) > m_Desc.maxBytes)
			{
				core::throw_runtime_error(
					"RangeBuffer '{}': an initial {} elements is already past the {} bytes its "
					"view can address",
					m_Desc.debugName,
					m_Desc.initialCount,
					m_Desc.maxBytes);
			}

			m_Storage.Init(
				std::move(resourceManager),
				m_Desc.debugName,
				sizeof(T),
				capacity,
				false,
				m_Desc.isRaw);

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

		// The live handle whose range starts at `rootIndex`, for a caller holding only the GPU-side
		// index -- the counterpart of EraseByIndex, without the erase.
		[[nodiscard]] core::multi_slot_handle
		HandleAt(uint32_t rootIndex) const
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(IsIndexValid(rootIndex), "HandleAt on an index with no live range");
			return m_Data.handle_at(rootIndex);
		}

		/**
		 * The mirror's own bytes for one live range, for a caller that writes at a finer
		 * granularity than T -- a byte arena over fixed-size blocks is the case, and staging
		 * through a scratch copy would double the cost of every vertex stream.
		 *
		 * @pre the handle is live. @post the whole range is marked dirty, so the caller may write
		 * any part of it and flush once; the span is invalidated by the next allocation on this
		 * buffer, which may reallocate the mirror.
		 */
		[[nodiscard]] std::span<std::byte>
		MutableRangeBytes(core::multi_slot_handle handle)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(IsValid(handle), "MutableRangeBytes on a range that is not live");

			MarkRangeDirty(handle.index, handle.count);

			auto* base =
				reinterpret_cast<std::byte*>(static_cast<T*>(m_Data.data()) + handle.index);
			return { base, static_cast<size_t>(handle.count) * sizeof(T) };
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

			const uint64_t totalBytes = static_cast<uint64_t>(m_Data.size()) * sizeof(T);

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
		//
		// @throws std::runtime_error if the arena has a byte ceiling and even the requested slots
		// would cross it.
		void
		Grow(uint32_t count)
		{
			const uint32_t grown = GrowCapacityFor(Capacity(), count, sizeof(T), m_Desc.maxBytes);

			if (grown == 0)
			{
				core::throw_runtime_error(
					"RangeBuffer '{}': {} more elements would take it to {} bytes, past the {} its "
					"view can address",
					m_Desc.debugName,
					count,
					(static_cast<uint64_t>(Capacity()) + count) * sizeof(T),
					m_Desc.maxBytes);
			}

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

		// 64-bit throughout: an arena at its byte ceiling addresses 2^32 bytes, and the products
		// below would wrap in 32 bits well before the assert on endBlock could notice.
		void
		MarkRangeDirty(uint32_t startIdx, uint32_t count)
		{
			if (count == 0)
				return;

			const auto [startBlock, endBlock] =
				FindDirtyBlocks(startIdx, count, sizeof(T), m_Desc.blockSize);

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
		IssueCopy(ICommandList* cmdList, uint32_t startBlk, uint32_t endBlk, uint64_t totalBytes)
		{
			const auto [offset, size] =
				MakeCopySlice(startBlk, endBlk, m_Desc.blockSize, totalBytes);

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
