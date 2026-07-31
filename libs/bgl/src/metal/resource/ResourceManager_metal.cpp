#include "resource/ResourceManager_metal.h"
#include "util/util.h"

#include <core/math.h>

#include "cmd/CommandList_metal.h"
#include "cmd/CommandQueue.h"
#include "util/util.h"

#include "util/util.h"
#include "util_metal.h"
#include <core/math.h>

#include <core/math.h>

namespace bgl
{
	ResourceManager::ResourceManager(MTL::Device* device, const ResourceManagerDesc& desc) :
		m_Device(device), m_Buffers(desc.maxCbvSrvUavs), m_Readbacks(desc.maxReadbackBuffers),
		m_Textures(desc.maxTextures), m_Rtvs(desc.maxRtvs), m_Dsvs(desc.maxDsvs),
		m_Samplers(desc.maxSamplers)
	{
		// Sizing the pools here is what makes the lock-free Get*/Valid* reads sound: a slot_vector
		// built with no capacity grows by emplace_back, which moves its storage out from under a
		// concurrent reader. Exhaustion returns a null handle instead, which every Create* reports.
		gassert(desc.maxCbvSrvUavs > 0, "maxCbvSrvUavs must be greater than zero");
		gassert(desc.maxTextures > 0, "maxTextures must be greater than zero");
		gassert(desc.maxReadbackBuffers > 0, "maxReadbackBuffers must be greater than zero");
		gassert(desc.maxRtvs > 0, "maxRtvs must be greater than zero");
		gassert(desc.maxDsvs > 0, "maxDsvs must be greater than zero");
		gassert(desc.maxSamplers > 0, "maxSamplers must be greater than zero");
	}

	BufferHandle
	ResourceManager::CreateStructBuffer(const StructBufferDesc& desc) noexcept
	{
		gassert(desc.stride > 0, "StructuredBuffer requires a valid stride");
		gassert(desc.elementCount > 0, "StructuredBuffer requires a valid element count");

		BufferDesc bufferDesc;
		bufferDesc.byteSize  = static_cast<uint64_t>(desc.stride) * desc.elementCount;
		bufferDesc.isUav     = desc.isUav;
		bufferDesc.debugName = desc.debugName;

		std::lock_guard<std::mutex> lock(m_PoolMutex);

		const auto slot = m_Buffers.try_allocate_and_emplace(m_Device, bufferDesc);
		if (slot.is_null())
		{
			logger::error("CreateStructBuffer '{}': buffer pool exhausted", desc.debugName);
			return BufferHandle{};
		}
		return BufferHandle{ slot };
	}

	BufferHandle
	ResourceManager::CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept
	{
		// A compute buffer is a GPU-only structured buffer with UAV access; reuse the
		// structured-buffer path to create it.
		StructBufferDesc sbDesc;
		sbDesc.stride       = desc.elementSize;
		sbDesc.elementCount = desc.initialCount;
		sbDesc.isUav        = true;
		sbDesc.debugName    = desc.debugName;
		return CreateStructBuffer(sbDesc);
	}

	ReadbackBufferHandle
	ResourceManager::CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);

		const auto slot = m_Readbacks.try_allocate_and_emplace(m_Device, desc);
		if (slot.is_null())
		{
			logger::error("CreateReadbackBuffer '{}': readback pool exhausted", desc.debugName);
			return ReadbackBufferHandle{};
		}
		return ReadbackBufferHandle{ slot };
	}

	void
	ResourceManager::RegisterQueue(ICommandQueue* queue) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(queue != nullptr, "RegisterQueue requires a non-null queue");
		gassert(
			m_RegisteredQueues.size() < c_MaxRegisteredQueues,
			"More than c_MaxRegisteredQueues submission timelines registered");
		m_RegisteredQueues.push_back(queue);
	}

	void
	ResourceManager::UnregisterQueue(ICommandQueue* queue) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		for (uint32_t i = 0; i < m_RegisteredQueues.size(); ++i)
		{
			if (m_RegisteredQueues[i] == queue)
			{
				m_RegisteredQueues[i] = m_RegisteredQueues.back();
				m_RegisteredQueues.pop_back();
				break;
			}
		}

		// The queue has drained (its owner flushes before unregistering), so it no longer gates any
		// pending free. Drop it from every batch's gate, or a freed queue's pointer would linger and,
		// if a later queue reused the address, alias it. An emptied gate is reclaimed next sweep.
		for (PendingDeletionBatch& batch : m_PendingBatches)
		{
			for (uint32_t i = 0; i < batch.gate.size(); ++i)
			{
				if (batch.gate[i].queue == queue)
				{
					batch.gate[i] = batch.gate.back();
					batch.gate.pop_back();
					break;
				}
			}
		}
	}

	DeletionGate
	ResourceManager::CaptureGate() const noexcept
	{
		auto gate = DeletionGate();
		for (ICommandQueue* queue : m_RegisteredQueues)
			gate.push_back({ queue, queue->GetNextFenceValue() });
		return gate;
	}

	void
	ResourceManager::RetireDeferred(PendingType type, uint32_t slotIndex) noexcept
	{
		const DeletionGate gate = CaptureGate();

		// Coalesce with the most recent batch when the gate has not moved -- fences only advance, so
		// consecutive destroys within a frame share one gate rather than each storing a copy.
		if (m_PendingBatches.empty() || !std::ranges::equal(m_PendingBatches.back().gate, gate))
			m_PendingBatches.push_back({ gate, {} });

		m_PendingBatches.back().deletions.push_back({ type, slotIndex });
	}

	void
	ResourceManager::DestroyBuffer(BufferHandle handle, bool deferred) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidBufferHandle(handle), "Cannot destroy invalid buffer handle");

		if (deferred)
		{
			m_Buffers.retire_slot(handle.slot.index);
			RetireDeferred(PendingType::kBuffer, handle.slot.index);
		}
		else
		{
			m_Buffers.release_slot(handle.slot);
		}
	}

	void
	ResourceManager::DestroyReadbackBuffer(ReadbackBufferHandle handle, bool deferred) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidReadbackBufferHandle(handle), "Cannot destroy invalid readback handle");

		if (deferred)
		{
			m_Readbacks.retire_slot(handle.slot.index);
			RetireDeferred(PendingType::kReadback, handle.slot.index);
		}
		else
		{
			m_Readbacks.release_slot(handle.slot);
		}
	}

	void
	ResourceManager::CleanupExpiredResources() noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);

		// Poll each queue once, not per pending deletion.
		auto completed = core::static_vector<QueueGate, c_MaxRegisteredQueues>();
		for (ICommandQueue* queue : m_RegisteredQueues)
			completed.push_back({ queue, queue->PollCurrentFenceValue() });

		const auto isCleared = [&](const QueueGate& entry) {
			for (const auto& [queue, value] : completed)
			{
				if (queue == entry.queue)
					return value >= entry.fenceValue;
			}
			// The gated queue is no longer registered: its context flushed and went away, so the
			// work that could have referenced this resource has completed.
			return true;
		};

		const auto reclaim = [&](const PendingDeletion& pending) {
			switch (pending.type)
			{
			case PendingType::kBuffer:
				m_Buffers.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kReadback:
				m_Readbacks.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kTexture:
				m_Textures.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kRtv:
				m_Rtvs.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kDsv:
				m_Dsvs.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kSampler:
				m_Samplers.reclaim_slot(pending.slotIndex);
				break;
			}
		};

		// A batch's slots were retired when they were recorded, so their handles have been stale
		// since then. Freeing the batch reclaims each index into its pool's free list.
		std::erase_if(m_PendingBatches, [&](const PendingDeletionBatch& batch) {
			if (!std::ranges::all_of(batch.gate, isCleared))
				return false;
			for (const PendingDeletion& pending : batch.deletions) reclaim(pending);
			return true;
		});
	}

	const Buffer&
	ResourceManager::GetBuffer(BufferHandle handle) const noexcept
	{
		return m_Buffers[handle.slot];
	}

	const ReadbackBuffer&
	ResourceManager::GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept
	{
		return m_Readbacks[handle.slot];
	}

	const void*
	ResourceManager::MapReadback(ReadbackBufferHandle handle) noexcept
	{
		return m_Readbacks[handle.slot].GetData();
	}

	void
	ResourceManager::UnmapReadback(ReadbackBufferHandle) noexcept
	{
		// Shared storage on unified memory: contents() stays valid, nothing to unmap.
	}

	bool
	ResourceManager::ValidBufferHandle(const BufferHandle& handle) const noexcept
	{
		return !handle.IsNull() && m_Buffers.valid(handle.slot);
	}

	bool
	ResourceManager::ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept
	{
		return !handle.IsNull() && m_Readbacks.valid(handle.slot);
	}

	TextureHandle
	ResourceManager::CreateTexture(const TextureDesc& desc) noexcept
	{
		m_LiveTexturesDirty = true;
		std::lock_guard<std::mutex> lock(m_PoolMutex);

		const auto slot = m_Textures.try_allocate_and_emplace(m_Device, desc);
		if (slot.is_null())
		{
			logger::error("CreateTexture '{}': texture pool exhausted", desc.debugName);
			return TextureHandle{};
		}
		return TextureHandle{ slot, desc.usage };
	}

	RtvHandle
	ResourceManager::CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept
	{
		gassert(ValidTextureHandle(textureHandle), "CreateRtv on an invalid texture");

		std::lock_guard<std::mutex> lock(m_PoolMutex);
		const auto                  slot = m_Rtvs.try_allocate_and_emplace(desc, textureHandle);
		if (slot.is_null())
		{
			logger::error("CreateRtv '{}': RTV pool exhausted", desc.debugName);
			return RtvHandle{};
		}
		return RtvHandle{ slot.index, slot.generation };
	}

	void
	ResourceManager::DestroyTexture(TextureHandle handle, bool deferred) noexcept
	{
		m_LiveTexturesDirty = true;
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidTextureHandle(handle), "Cannot destroy invalid texture handle");

		if (deferred)
		{
			m_Textures.retire_slot(handle.slot.index);
			RetireDeferred(PendingType::kTexture, handle.slot.index);
		}
		else
		{
			m_Textures.release_slot(handle.slot);
		}
	}

	void
	ResourceManager::DestroyRtv(RtvHandle handle, bool deferred) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidRtvHandle(handle), "Cannot destroy invalid RTV handle");

		// An RTV owns no allocation -- it is a view onto a texture -- so only its slot is gated.
		if (deferred)
		{
			m_Rtvs.retire_slot(handle.idx);
			RetireDeferred(PendingType::kRtv, handle.idx);
		}
		else
		{
			m_Rtvs.release_slot(handle.idx);
		}
	}

	const Texture&
	ResourceManager::GetTexture(TextureHandle handle) const noexcept
	{
		return m_Textures[handle.slot];
	}

	TextureDesc
	ResourceManager::GetTextureDesc(TextureHandle handle) const noexcept
	{
		gassert(ValidTextureHandle(handle), "GetTextureDesc on an invalid texture handle");
		return m_Textures[handle.slot].GetDesc();
	}

	const Rtv&
	ResourceManager::GetRtv(RtvHandle handle) const noexcept
	{
		// RtvHandle carries its own generation (it is not a slot_handle), so the raw-index lookup
		// below can't check it -- validate explicitly, as GetTexture gets for free from slot_handle.
		gassert(ValidRtvHandle(handle), "Invalid RTV handle");
		return m_Rtvs[handle.idx];
	}

	TextureHandle
	ResourceManager::GetRtvTexture(RtvHandle handle) const noexcept
	{
		return GetRtv(handle).GetTextureHandle();
	}

	TextureReadbackLayout
	ResourceManager::GetTextureReadbackLayout(TextureHandle handle) const noexcept
	{
		const TextureDesc& desc = GetTexture(handle).GetDesc();

		// Metal has no 256-byte row-pitch rule (unlike D3D12), so the rows pack tight.
		TextureReadbackLayout layout;
		const FormatInfo      format = GetFormatInfo(desc.format);

		layout.rowSizeBytes =
			core::div_ceil<uint64_t>(desc.width, format.blockEdgeTexels) * format.bytesPerBlock;
		layout.rowPitch   = layout.rowSizeBytes;
		layout.rowCount   = core::div_ceil<uint64_t>(desc.height, format.blockEdgeTexels);
		layout.offset     = 0;
		layout.totalBytes = layout.rowPitch * layout.rowCount;
		return layout;
	}

	bool
	ResourceManager::ValidTextureHandle(const TextureHandle& handle) const noexcept
	{
		return !handle.IsNull() && m_Textures.valid(handle.slot) &&
		       !m_Textures[handle.slot].IsNull();
	}

	bool
	ResourceManager::ValidRtvHandle(const RtvHandle& handle) const noexcept
	{
		return !handle.IsNull() && m_Rtvs.valid(handle.idx, handle.generation) &&
		       !m_Rtvs[handle.idx].IsNull();
	}

	void
	ResourceManager::ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4]) noexcept
	{
		gassert(ValidRtvHandle(handle), "ClearRtv on an invalid RTV handle");
		gassert(cmdList != nullptr && cmdList->IsOpen(), "ClearRtv needs an open command list");

		MTL::Texture* texture = GetTexture(GetRtv(handle).GetTextureHandle()).GetMTLResource();
		cmdList->As<CommandList>()->ClearRenderTarget(texture, clearVal);
	}
}

namespace bgl
{
	SamplerHandle
	ResourceManager::CreateSampler(const SamplerDesc& desc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);

		const auto slot = m_Samplers.try_allocate_and_emplace(m_Device, desc);
		if (slot.is_null())
		{
			logger::error("CreateSampler: sampler pool exhausted");
			return SamplerHandle{};
		}
		return SamplerHandle{ slot.index, slot.generation };
	}

	void
	ResourceManager::DestroySampler(SamplerHandle handle, bool deferred) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidSamplerHandle(handle), "Cannot destroy invalid sampler handle");

		if (deferred)
		{
			m_Samplers.retire_slot(handle.idx);
			RetireDeferred(PendingType::kSampler, handle.idx);
		}
		else
		{
			m_Samplers.release_slot(handle.idx);
		}
	}

	const Sampler&
	ResourceManager::GetSampler(SamplerHandle handle) const noexcept
	{
		gassert(ValidSamplerHandle(handle), "Invalid sampler handle");
		return m_Samplers[handle.idx];
	}

	bool
	ResourceManager::ValidSamplerHandle(const SamplerHandle& handle) const noexcept
	{
		return !handle.IsNull() && m_Samplers.valid(handle.idx, handle.generation) &&
		       !m_Samplers[handle.idx].IsNull();
	}

	DsvHandle
	ResourceManager::CreateDsv(TextureHandle textureHandle, const DsvDesc& desc) noexcept
	{
		gassert(ValidTextureHandle(textureHandle), "CreateDsv on an invalid texture");

		std::lock_guard<std::mutex> lock(m_PoolMutex);
		const auto                  slot = m_Dsvs.try_allocate_and_emplace(desc, textureHandle);
		if (slot.is_null())
		{
			logger::error("CreateDsv '{}': DSV pool exhausted", desc.debugName);
			return DsvHandle{};
		}
		return DsvHandle{ slot.index, slot.generation };
	}

	void
	ResourceManager::DestroyDsv(DsvHandle handle, bool deferred) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidDsvHandle(handle), "Cannot destroy invalid DSV handle");

		// Like an RTV, a DSV owns no allocation -- it names a texture -- so only its slot is gated.
		if (deferred)
		{
			m_Dsvs.retire_slot(handle.idx);
			RetireDeferred(PendingType::kDsv, handle.idx);
		}
		else
		{
			m_Dsvs.release_slot(handle.idx);
		}
	}

	const Dsv&
	ResourceManager::GetDsv(DsvHandle handle) const noexcept
	{
		gassert(ValidDsvHandle(handle), "Invalid DSV handle");
		return m_Dsvs[handle.idx];
	}

	TextureHandle
	ResourceManager::GetDsvTexture(DsvHandle handle) const noexcept
	{
		return GetDsv(handle).GetTextureHandle();
	}

	bool
	ResourceManager::ValidDsvHandle(const DsvHandle& handle) const noexcept
	{
		return !handle.IsNull() && m_Dsvs.valid(handle.idx, handle.generation) &&
		       !m_Dsvs[handle.idx].IsNull();
	}

	void
	ResourceManager::ClearDsv(
		ICommandList* cmdList,
		DsvHandle     handle,
		float         depth,
		uint8_t       stencil) noexcept
	{
		gassert(ValidDsvHandle(handle), "ClearDsv on an invalid DSV handle");
		gassert(cmdList != nullptr && cmdList->IsOpen(), "ClearDsv needs an open command list");

		MTL::Texture* texture = m_Textures[GetDsvTexture(handle).slot].GetMTLResource();
		cmdList->As<CommandList>()->ClearDepthStencil(texture, depth, stencil);
	}

	DescriptorHandle
	ResourceManager::ResolveDescriptor(const TextureHandle& handle) const noexcept
	{
		if (!ValidTextureHandle(handle))
			return {};

		return DescriptorHandle::FromNative(
			m_Textures[handle.slot].GetMTLResource()->gpuResourceID()._impl);
	}

	std::span<MTL::Resource* const>
	ResourceManager::GetLiveTextureResources() noexcept
	{
		// A texture whose id a shader reads out of GPU memory is invisible to the encoder, so the
		// encoder is told about all of them. Rebuilt only when the pool changes, not per draw.
		if (m_LiveTexturesDirty)
		{
			m_LiveTextures.clear();
			for (uint32_t i = 0; i < m_Textures.capacity(); ++i)
			{
				if (m_Textures.allocated(i) && !m_Textures[i].IsNull())
					m_LiveTextures.push_back(m_Textures[i].GetMTLResource());
			}
			m_LiveTexturesDirty = false;
		}
		return m_LiveTextures;
	}

	bool
	ResourceManager::IsTextureCube(const TextureHandle& handle) const noexcept
	{
		gassert(ValidTextureHandle(handle), "IsTextureCube on an invalid texture handle");
		const MTL::TextureType type = m_Textures[handle.slot].GetMTLResource()->textureType();
		return type == MTL::TextureTypeCube || type == MTL::TextureTypeCubeArray;
	}
}
