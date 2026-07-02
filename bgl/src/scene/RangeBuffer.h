#pragma once
#include "uniforms/DescriptorHandle.h"
#include <core/containers/multi_slot_vector.h>

namespace bgl
{
	struct RangeBufferDesc
	{
		uint32_t    maxCount  = 0;
		uint32_t    blockSize = 65536;  // Default to the sweet spot 64KB
		std::string debugName;
	};

	template <typename T>
	concept RangeBufferConcept =
		core::MultiSlotElementConcept<T> && core::type_traits::trivially_copyable<T>;

	/**
	 * A GPU-mirrored buffer of variable-length ranges of trivially-copyable
	 * elements.
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
		RangeBuffer(RangeBufferDesc desc, ResourceManagerHandle resourceManager) noexcept
		{
			Init(std::move(desc), std::move(resourceManager));
		}

		void
		Init(RangeBufferDesc desc, ResourceManagerHandle resourceManager) noexcept
		{
			gassert(desc.maxCount > 0, "RangeBuffer must have a positive count");
			gassert(desc.blockSize > 0, "Block size must be greater than zero");
			gassert(resourceManager != nullptr, "RangeBuffer requires a valid ResourceManager");

			m_Desc            = std::move(desc);
			m_ResourceManager = std::move(resourceManager);

			// Initialize the multi-slot tracking container
			m_Data.reset(m_Desc.maxCount);

			if constexpr (c_HasMeta)
			{
				m_Metadata.assign(m_Desc.maxCount, Meta{});
			}

			// Calculate tracking blocks based on total byte layout
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

		core::multi_slot_handle
		Add(std::span<const T> elem)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			gassert(
				elem.size() < std::numeric_limits<uint32_t>::max(),
				"Element count exceeds uint32_t limits");

			auto handle = AllocateRange(static_cast<uint32_t>(elem.size()));
			for (auto i = 0u; i < elem.size(); ++i)
			{
				m_Data[handle.index + i] = elem[i];
			}
			MarkRangeDirty(handle.index, handle.count);

			return handle;
		}

		[[nodiscard]]
		DescriptorHandle
		GetDescriptorHandle() const noexcept
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			return DescriptorHandle(m_BufferHandle.idx);
		}

		[[nodiscard]] BufferHandle
		GetBufferHandle() const noexcept
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			return m_BufferHandle;
		}

		[[nodiscard]] core::multi_slot_handle
		AllocateRange(uint32_t count)
		{
			gassert(IsInitialized(), "RangeBuffer is uninitialized; call Init() first");
			auto handle = m_Data.allocate_slots(count);

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
		// GPU-side index is known (db structs store indices, not generations).
		[[nodiscard]] bool
		IsIndexValid(uint32_t rootIndex) const noexcept
		{
			return m_Data.is_allocated_root(rootIndex);
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
		Release(uint64_t fenceValue, bool deferred = true) noexcept
		{
			if (!m_BufferHandle.IsNull())
			{
				m_ResourceManager->DestroyBuffer(m_BufferHandle, fenceValue, deferred);
				m_BufferHandle = {};
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

			if (offset + size > totalBytes)
			{
				size = totalBytes - offset;
			}

			if (size > 0)
			{
				cmdList->WriteBuffer(m_BufferHandle, m_Data.data(), offset, size);
			}
		}

	private:
		RangeBufferDesc       m_Desc;
		ResourceManagerHandle m_ResourceManager;
		BufferHandle          m_BufferHandle;

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
