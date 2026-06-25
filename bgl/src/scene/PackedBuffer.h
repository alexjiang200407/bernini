#pragma once
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "uniforms/DescriptorHandle.h"
#include <core/containers/packed_vector.h>
#include <core/containers/slot_vector.h>
#include <core/type_traits.h>

namespace bgl
{
	struct PackedBufferDesc
	{
		uint32_t    maxCount  = 0;
		uint32_t    blockSize = 65536;
		std::string debugName;
	};

	enum class PackedBufferState
	{
		kUpdate,
		kShader,
		kNone,
	};

	template <typename T>
	concept PackedBufferConcept =
		core::PackedElementConcept<T> && core::type_traits::trivially_copyable<T>;

	template <PackedBufferConcept T>
	class PackedBuffer
	{
	public:
		using Handle = core::slot_handle;

	public:
		PackedBuffer() noexcept = default;
		PackedBuffer(PackedBufferDesc desc, ResourceManagerHandle resourceManager) noexcept
		{
			Init(std::move(desc), std::move(resourceManager));
		}

		void
		Init(PackedBufferDesc desc, ResourceManagerHandle resourceManager) noexcept
		{
			gassert(desc.maxCount > 0, "PackedBuffer must have a positive maxCount");
			gassert(desc.blockSize > 0, "Block size must be greater than zero");
			gassert(resourceManager != nullptr, "PackedBuffer requires a valid ResourceManager");

			m_Desc            = std::move(desc);
			m_ResourceManager = std::move(resourceManager);

			m_Entries.reset(m_Desc.maxCount);
			m_HandleToIndex.reset(m_Desc.maxCount);
			m_IndexToHandle.assign(m_Desc.maxCount, core::slot_handle::invalid_index);

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

		template <typename... Args>
		Handle
		EmplaceBack(Args&&... args)
		{
			uint32_t denseIndex = m_Entries.emplace_back(std::forward<Args>(args)...);
			return Register(denseIndex);
		}

		Handle
		Add(T value)
		{
			return EmplaceBack(std::move(value));
		}

		uint32_t
		Size() const noexcept
		{
			return m_Entries.size();
		}

		void
		Set(Handle handle, T value)
		{
			gassert(IsValid(handle), "Invalid PackedBuffer handle");

			uint32_t denseIndex   = m_HandleToIndex[handle.index];
			m_Entries[denseIndex] = std::move(value);
			MarkDirty(denseIndex);
		}

		const T&
		operator[](Handle handle) const
		{
			gassert(IsValid(handle), "Invalid PackedBuffer handle");
			return m_Entries[m_HandleToIndex[handle.index]];
		}

		void
		Erase(Handle handle)
		{
			gassert(IsValid(handle), "Invalid PackedBuffer handle");

			uint32_t denseIndex = m_HandleToIndex[handle.index];
			uint32_t moved      = m_Entries.erase(denseIndex);

			if (moved != core::packed_vector<T>::invalid_index)
			{
				uint32_t movedHandle         = m_IndexToHandle[moved];
				m_HandleToIndex[movedHandle] = denseIndex;
				m_IndexToHandle[denseIndex]  = movedHandle;
				MarkDirty(denseIndex);
			}

			m_HandleToIndex.release_slot(handle.index);
		}

		[[nodiscard]] bool
		IsValid(Handle handle) const
		{
			return m_HandleToIndex.valid(handle.index, handle.generation);
		}

		[[nodiscard]] uint32_t
		Count() const noexcept
		{
			return m_Entries.size();
		}

		[[nodiscard]] bool
		IsEmpty() const noexcept
		{
			return m_Entries.empty();
		}

		void
		Transition(ICommandList* cmdList, PackedBufferState prevState, PackedBufferState newState)
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
			gassert(cmdList->IsOpen(), "ICommandList must be open to update PackedBuffer");

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
		Handle
		Register(uint32_t denseIndex)
		{
			Handle handle               = m_HandleToIndex.allocate_and_emplace(denseIndex);
			m_IndexToHandle[denseIndex] = handle.index;
			MarkDirty(denseIndex);
			return handle;
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
		GetBarrierAccessSync(PackedBufferState state)
		{
			switch (state)
			{
			case PackedBufferState::kUpdate:
				return { BarrierAccessFlag::kCopyDest, BarrierSyncFlag::kCopy };
			case PackedBufferState::kShader:
				return { BarrierAccessFlag::kShaderResource, BarrierSyncFlag::kVertexShader };
			case PackedBufferState::kNone:
				return { BarrierAccessFlag::kNone, BarrierSyncFlag::kNone };
			default:
				gfatal("Invalid PackedBufferState");
			}
		}

	private:
		PackedBufferDesc       m_Desc;
		ResourceManagerHandle  m_ResourceManager;
		BufferHandle           m_BufferHandle;
		core::packed_vector<T> m_Entries;

		// Stable-handle indirection (see the class comment).
		core::slot_vector<uint32_t> m_HandleToIndex;
		std::vector<uint32_t>       m_IndexToHandle;

		std::vector<bool> m_DirtyBlocks;
		bool              m_HasAnyDirtyBlocks = false;
	};

	template <typename T>
	struct is_packed_buffer : std::false_type
	{};

	template <typename... Args>
	struct is_packed_buffer<PackedBuffer<Args...>> : std::true_type
	{};

	template <typename T>
	inline constexpr bool is_packed_buffer_v = is_packed_buffer<std::decay_t<T>>::value;
}
