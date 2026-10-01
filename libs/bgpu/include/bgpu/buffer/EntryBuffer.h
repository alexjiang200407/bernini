#pragma once
#include <algorithm>
#include <bgpu/buffer/GrowableGpuBuffer.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/uniforms/DescriptorHandle.h>
#include <core/containers/slot_handle.h>
#include <core/containers/slot_vector.h>
#include <core/err/util.h>
#include <core/type_traits.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace bgpu
{
	struct EntryBufferDesc
	{
		// Where the arena starts, not where it ends: it grows on demand and is bounded only by
		// device memory. The reserved null element is carried on top of this, so a caller's budget
		// is entirely its own.
		uint32_t    initialCount = 0;
		uint32_t    blockSize    = 65536;
		std::string debugName;

		// The buffer also has a writable view (GetWritableView) for a pass that writes elements the
		// CPU claimed with ClaimRange and never changes.
		bool writableView = false;

		template <typename Self>
		Self&&
		SetInitialCount(this Self&& self, uint32_t value) noexcept
		{
			self.initialCount = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetBlockSize(this Self&& self, uint32_t value) noexcept
		{
			self.blockSize = value;
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetDebugName(this Self&& self, std::string value) noexcept
		{
			self.debugName = std::move(value);
			return std::forward<Self>(self);
		}

		template <typename Self>
		Self&&
		SetWritableView(this Self&& self, bool value = true) noexcept
		{
			self.writableView = value;
			return std::forward<Self>(self);
		}
	};

	// Elements [first, first + count) of an EntryBuffer, claimed together and addressed by index.
	struct EntryRange
	{
		uint32_t first = 0;
		uint32_t count = 0;
	};

	template <typename T>
	concept EntryBufferConcept =
		core::SlotElementConcept<T> && core::type_traits::trivially_copyable<T>;

	/**
	 * A GPU-mirrored slot buffer of trivially-copyable elements.
	 *
	 * Element 0 is reserved and never allocated, so the index in every live
	 * `idl::Entry` is distinguishable from a null one.
	 */
	template <EntryBufferConcept T, typename Meta = void>
	class EntryBuffer
	{
	private:
		static constexpr bool c_HasMeta = !std::is_void_v<Meta>;
		using MetaElem                  = std::conditional_t<c_HasMeta, Meta, int>;
		using MetaStorage = std::conditional_t<c_HasMeta, std::vector<MetaElem>, std::monostate>;

	public:
		EntryBuffer(ResourceManagerRef resourceManager, EntryBufferDesc desc) :
			m_Desc(std::move(desc)), m_ResourceManager(resourceManager),
			m_Storage(
				std::move(resourceManager),
				m_Desc.debugName,
				sizeof(T),
				m_Desc.initialCount + 1,
				false,
				false,
				m_Desc.writableView)
		{
			core::ensure(m_Desc.initialCount > 0, "EntryBuffer must have a positive initial count");
			core::ensure(m_Desc.blockSize > 0, "Block size must be greater than zero");

			const uint32_t capacity = m_Desc.initialCount + 1;

			m_Entries.reset(capacity);

			if constexpr (c_HasMeta)
			{
				m_Metadata.assign(capacity, Meta{});
			}

			ResizeDirtyBlocks(capacity);
			ReserveNullEntry();
			RefreshWritableView();
		}

		~EntryBuffer() noexcept { DestroyWritableView(); }

		EntryBuffer(const EntryBuffer&) = delete;

		EntryBuffer(EntryBuffer&& other) noexcept :
			m_Desc(std::move(other.m_Desc)), m_ResourceManager(std::move(other.m_ResourceManager)),
			m_Storage(std::move(other.m_Storage)), m_Entries(std::move(other.m_Entries)),
			m_Metadata(std::move(other.m_Metadata)), m_DirtyBlocks(std::move(other.m_DirtyBlocks)),
			m_HasAnyDirtyBlocks(other.m_HasAnyDirtyBlocks), m_Ranges(std::move(other.m_Ranges)),
			m_WritableView(std::exchange(other.m_WritableView, {})),
			m_ViewedBuffer(std::exchange(other.m_ViewedBuffer, {}))
		{}

		EntryBuffer&
		operator=(const EntryBuffer&) = delete;

		EntryBuffer&
		operator=(EntryBuffer&& other) noexcept
		{
			if (this != &other)
			{
				DestroyWritableView();
				m_Desc              = std::move(other.m_Desc);
				m_ResourceManager   = std::move(other.m_ResourceManager);
				m_Storage           = std::move(other.m_Storage);
				m_Entries           = std::move(other.m_Entries);
				m_Metadata          = std::move(other.m_Metadata);
				m_DirtyBlocks       = std::move(other.m_DirtyBlocks);
				m_HasAnyDirtyBlocks = other.m_HasAnyDirtyBlocks;
				m_Ranges            = std::move(other.m_Ranges);
				m_WritableView      = std::exchange(other.m_WritableView, {});
				m_ViewedBuffer      = std::exchange(other.m_ViewedBuffer, {});
			}
			return *this;
		}

		[[nodiscard]] bool
		IsValid(core::slot_handle handle) const noexcept
		{
			return m_Entries.valid(handle.index, handle.generation);
		}

		// The reserved null element is allocated but belongs to no caller, so an offset read back
		// from a GPU-side struct answers false for it rather than resolving to element 0.
		[[nodiscard]] bool
		IsIndexValid(uint32_t index) const noexcept
		{
			return index != 0 && m_Entries.allocated(index);
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
			auto slot = EmplaceBack();
			Set(slot, std::move(value));
			return slot;
		}

		void
		Set(core::slot_handle slot, T value) noexcept
		{
			core::ensure(m_Entries.valid(slot.index, slot.generation), "Invalid slot handle");
			MarkDirty(slot.index);
			m_Entries[slot.index] = std::move(value);
		}

		const T&
		operator[](core::slot_handle slot) const noexcept
		{
			core::ensure(m_Entries.valid(slot.index, slot.generation), "Invalid slot handle");
			return m_Entries[slot.index];
		}

		const T&
		AtIndex(uint32_t index) const noexcept
		{
			core::ensure(m_Entries.allocated(index), "AtIndex on an unallocated slot");
			return m_Entries[index];
		}

		void
		Erase(core::slot_handle slot) noexcept
		{
			core::ensure(m_Entries.valid(slot.index, slot.generation), "Invalid slot handle");
			m_Entries.release_slot(slot.index);
		}

		void
		EraseByIndex(uint32_t index) noexcept
		{
			core::ensure(m_Entries.allocated(index), "EraseByIndex on an unallocated slot");
			m_Entries.release_slot(index);
		}

		/**
		 * Claims `count` consecutive elements past every existing one, each holding `value`, for a
		 * caller that addresses them by index rather than by handle. The run starts on an upload
		 * block and is padded to the end of its last, so no other element shares a block with it:
		 * it is uploaded once here and never again because a neighbour changed, which is what lets
		 * a pass write it on the GPU (GetWritableView).
		 *
		 * @pre the desc's blockSize is a multiple of sizeof(T).
		 * @throws std::runtime_error if the buffer cannot grow to hold the run; nothing is claimed.
		 */
		EntryRange
		ClaimRange(uint32_t count, const T& value)
		{
			core::ensure(count > 0, "ClaimRange of no elements");
			const uint32_t perBlock = ElementsPerBlock();
			const uint32_t first    = AlignUp(Capacity(), perBlock);
			const uint32_t end      = AlignUp(first + count, perBlock);
			core::ensure(first + count > first && end >= first + count, "ClaimRange past 2^32");

			GrowTo(end);
			const bool claimed = m_Entries.try_claim_range(first, end - first);
			core::ensure(claimed, "A range past every element must be free");

			for (uint32_t index = first; index < end; ++index)
			{
				if (index < first + count)
				{
					m_Entries[index] = value;
				}
				ResetMeta(index);
				MarkDirty(index);
			}
			m_Ranges.push_back(EntryRange{ first, count });
			return m_Ranges.back();
		}

		/**
		 * Releases a run ClaimRange returned, padding included. A pass that wrote it must not run
		 * again: the indices go back to single allocation and their next owner uploads over them.
		 *
		 * @throws std::runtime_error, releasing nothing, if `range` is not a claimed run.
		 */
		void
		ReleaseRange(EntryRange range)
		{
			const auto claim = std::ranges::find_if(m_Ranges, [range](const EntryRange& r) {
				return r.first == range.first && r.count == range.count;
			});
			if (claim == m_Ranges.end())
			{
				core::throw_runtime_error(
					"EntryBuffer '{}': [{}, +{}) is not a claimed range",
					m_Desc.debugName,
					range.first,
					range.count);
			}
			const uint32_t end = AlignUp(range.first + range.count, ElementsPerBlock());
			m_Entries.release_range(range.first, end - range.first);
			m_Ranges.erase(claim);
		}

		/**
		 * The buffer as a writable structured view, for a pass that writes the elements of a
		 * ClaimRange run on the GPU. Null without the desc's writableView. Re-read every frame, as
		 * GetDescriptorHandle is: a growth replaces it.
		 */
		[[nodiscard]] BufferUavHandle
		GetWritableView() const noexcept
		{
			return m_WritableView;
		}

		template <typename M = Meta>
		[[nodiscard]] M&
		MetaAt(uint32_t index) noexcept
			requires(!std::is_void_v<M>)
		{
			core::ensure(m_Entries.allocated(index), "MetaAt on an unallocated slot");
			return m_Metadata[index];
		}

		template <typename M = Meta>
		[[nodiscard]] const M&
		MetaAt(uint32_t index) const noexcept
			requires(!std::is_void_v<M>)
		{
			core::ensure(m_Entries.allocated(index), "MetaAt on an unallocated slot");
			return m_Metadata[index];
		}

		void
		Update(ICommandList* cmdList) noexcept
		{
			core::ensure(cmdList != nullptr, "Update requires a valid ICommandList");
			core::ensure(cmdList->IsOpen(), "ICommandList must be open to update EntryBuffer");

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
			return DescriptorHandle(m_Storage.GetHandle().bindlessIndex);
		}

		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
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

	private:
		// Held for the buffer's lifetime so no caller is handed the offset that means null. Marked
		// dirty so the GPU sees a zeroed element there rather than whatever the allocation held.
		void
		ReserveNullEntry()
		{
			const core::slot_handle slot = m_Entries.try_allocate_and_emplace();
			core::ensure(slot.index == 0, "The null entry must own the first element");
			MarkDirty(slot.index);
		}

		void
		Grow()
		{
			GrowTo(NextGpuBufferCapacity(Capacity(), Capacity() + 1, sizeof(T)));
		}

		void
		GrowTo(uint32_t capacity)
		{
			if (capacity <= Capacity())
				return;

			// GPU side first: it is the one that can fail, and it leaves nothing behind when it
			// does, so the mirror and the buffer cannot end up disagreeing on capacity.
			m_Storage.Grow(capacity);
			m_Entries.grow(capacity);

			if constexpr (c_HasMeta)
			{
				m_Metadata.resize(capacity, Meta{});
			}

			ResizeDirtyBlocks(capacity);

			// Here rather than at a frame boundary, so the buffer and its view are never
			// observable apart (docs/rhi.md).
			RefreshWritableView();
		}

		[[nodiscard]] uint32_t
		ElementsPerBlock() const noexcept
		{
			core::ensure(
				m_Desc.blockSize % sizeof(T) == 0,
				"A range is block-aligned only when a block holds whole elements");
			return static_cast<uint32_t>(m_Desc.blockSize / sizeof(T));
		}

		[[nodiscard]] static uint32_t
		AlignUp(uint32_t value, uint32_t alignment) noexcept
		{
			return (value + alignment - 1) / alignment * alignment;
		}

		// A no-op without a writable view, and for a buffer that has not been replaced.
		void
		RefreshWritableView()
		{
			if (!m_Desc.writableView || m_ResourceManager == nullptr)
				return;

			const BufferHandle buffer = m_Storage.GetHandle();
			if (buffer.slot == m_ViewedBuffer.slot &&
			    buffer.bindlessIndex == m_ViewedBuffer.bindlessIndex)
				return;

			DestroyWritableView();
			m_WritableView = m_ResourceManager->CreateBufferUav(
				buffer,
				BufferUavDesc().SetElement<T>().SetDebugName(m_Desc.debugName + " (writable)"));
			if (m_WritableView.IsNull())
			{
				core::throw_runtime_error(
					"EntryBuffer '{}': no writable view could be made (maxBufferUavs)",
					m_Desc.debugName);
			}
			m_ViewedBuffer = buffer;
		}

		// A moved-from buffer has no manager and no view.
		void
		DestroyWritableView() noexcept
		{
			if (m_ResourceManager != nullptr && !m_WritableView.IsNull())
			{
				m_ResourceManager->DestroyBufferUav(m_WritableView);
				m_WritableView = BufferUavHandle{};
			}
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

			core::ensure(
				endBlock < m_DirtyBlocks.size(),
				"Dirty tracking index out of block bounds");

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
		ResourceManagerRef   m_ResourceManager;
		GrowableGpuBuffer    m_Storage;
		core::slot_vector<T> m_Entries;

		MetaStorage m_Metadata;

		std::vector<bool> m_DirtyBlocks;
		bool              m_HasAnyDirtyBlocks = false;

		// Every live ClaimRange, so a release names one exactly rather than any aligned run.
		std::vector<EntryRange> m_Ranges;

		BufferUavHandle m_WritableView;
		BufferHandle    m_ViewedBuffer;
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
