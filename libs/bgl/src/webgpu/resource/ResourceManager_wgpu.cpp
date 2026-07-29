#include "resource/ResourceManager_wgpu.h"

#include "cmd/CommandList_wgpu.h"
#include "cmd/CommandQueue.h"
#include "convert_wgpu.h"
#include "util/util.h"

#include <core/math.h>

namespace bgl
{
	ResourceManager::ResourceManager(
		const wgpu::Device&        device,
		const wgpu::Instance&      instance,
		const ResourceManagerDesc& desc) :
		m_Device(device), m_Instance(instance), m_Buffers(desc.maxCbvSrvUavs),
		m_ReadbackBuffers(desc.maxReadbackBuffers), m_Textures(desc.maxCbvSrvUavs),
		m_Samplers(desc.maxSamplers), m_Rtvs(desc.maxRtvs), m_Dsvs(desc.maxDsvs)
	{
		gassert(m_Device != nullptr, "ResourceManager: null device");
	}

	BufferHandle
	ResourceManager::CreateStructBuffer(const StructBufferDesc& desc) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		const auto slot = m_Buffers.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("ResourceManager: buffer pool exhausted creating '{}'", desc.debugName);
			return BufferHandle{ core::slot_handle{} };
		}

		auto bufferDesc      = BufferDesc{};
		bufferDesc.byteSize  = uint64_t{ desc.stride } * desc.elementCount;
		bufferDesc.isUav     = desc.isUav;
		bufferDesc.debugName = desc.debugName;

		m_Buffers[slot.index] = Buffer(m_Device, bufferDesc);

		return BufferHandle{ slot };
	}

	BufferHandle
	ResourceManager::CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept
	{
		auto structDesc   = StructBufferDesc{}
		                        .SetElementCount(desc.initialCount)
		                        .SetIsUav(true)
		                        .SetDebugName(desc.debugName);
		structDesc.stride = desc.elementSize;

		return CreateStructBuffer(structDesc);
	}

	ReadbackBufferHandle
	ResourceManager::CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		const auto slot = m_ReadbackBuffers.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("ResourceManager: readback pool exhausted creating '{}'", desc.debugName);
			return ReadbackBufferHandle{ core::slot_handle{} };
		}

		m_ReadbackBuffers[slot.index] = ReadbackBuffer(m_Device, m_Instance, desc);

		return ReadbackBufferHandle{ slot };
	}

	void
	ResourceManager::RegisterQueue(ICommandQueue* queue) noexcept
	{
		gassert(queue != nullptr, "RegisterQueue: null queue");

		auto lock = std::scoped_lock(m_PoolMutex);

		gassert(
			m_Queues.size() < c_MaxRegisteredQueues,
			"More than c_MaxRegisteredQueues submission timelines registered");
		m_Queues.push_back(queue);
	}

	void
	ResourceManager::UnregisterQueue(ICommandQueue* queue) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		const auto it = std::ranges::find(m_Queues, queue);
		if (it != m_Queues.end())
		{
			std::move(it + 1, m_Queues.end(), it);
			m_Queues.pop_back();
		}

		// Scrub it out of every live gate too: a freed queue pointer could otherwise alias a
		// later queue allocated at the same address and gate a free on the wrong timeline.
		for (auto& batch : m_PendingBatches)
		{
			const auto stale =
				std::remove_if(batch.gate.begin(), batch.gate.end(), [queue](const QueueGate& g) {
					return g.queue == queue;
				});

			for (auto n = std::distance(stale, batch.gate.end()); n > 0; --n) batch.gate.pop_back();
		}
	}

	ResourceManager::DeletionGate
	ResourceManager::CaptureGate() const noexcept
	{
		auto gate = DeletionGate();

		for (ICommandQueue* queue : m_Queues) gate.push_back({ queue, queue->GetNextFenceValue() });

		return gate;
	}

	void
	ResourceManager::RetireDeferred(PendingType type, uint32_t slotIndex) noexcept
	{
		auto gate = CaptureGate();

		// A burst of frees in one frame shares one gate rather than one batch each.
		if (!m_PendingBatches.empty() && std::ranges::equal(m_PendingBatches.back().gate, gate))
		{
			m_PendingBatches.back().deletions.push_back({ type, slotIndex });
			return;
		}

		m_PendingBatches.push_back({ std::move(gate), { { type, slotIndex } } });
	}

	void
	ResourceManager::DestroyBuffer(BufferHandle handle, bool deferred) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		if (!m_Buffers.valid(handle.slot))
			return;

		if (deferred)
		{
			m_Buffers.retire_slot(handle.slot);
			RetireDeferred(PendingType::kBuffer, handle.slot.index);
			return;
		}

		m_Buffers.release_slot(handle.slot);
	}

	void
	ResourceManager::DestroyReadbackBuffer(ReadbackBufferHandle handle, bool deferred) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		if (!m_ReadbackBuffers.valid(handle.slot))
			return;

		if (deferred)
		{
			m_ReadbackBuffers.retire_slot(handle.slot);
			RetireDeferred(PendingType::kReadback, handle.slot.index);
			return;
		}

		m_ReadbackBuffers.release_slot(handle.slot);
	}

	void
	ResourceManager::CleanupExpiredResources() noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		auto completed = std::vector<std::pair<ICommandQueue*, uint64_t>>();
		completed.reserve(m_Queues.size());

		for (ICommandQueue* queue : m_Queues)
			completed.emplace_back(queue, queue->PollCurrentFenceValue());

		const auto isCleared = [&completed](const QueueGate& gate) {
			const auto it =
				std::ranges::find(completed, gate.queue, [](const auto& e) { return e.first; });

			// A gate on a queue that is no longer registered counts as cleared: its context
			// flushed and went away, so that timeline has drained.
			return it == completed.end() || it->second >= gate.fenceValue;
		};

		std::erase_if(m_PendingBatches, [&](const PendingBatch& batch) {
			if (!std::ranges::all_of(batch.gate, isCleared))
				return false;

			for (const PendingDeletion& deletion : batch.deletions)
			{
				switch (deletion.type)
				{
				case PendingType::kBuffer:
					m_Buffers.reclaim_slot(deletion.slotIndex);
					break;
				case PendingType::kReadback:
					m_ReadbackBuffers.reclaim_slot(deletion.slotIndex);
					break;
				case PendingType::kTexture:
					m_Textures.reclaim_slot(deletion.slotIndex);
					break;
				case PendingType::kSampler:
					m_Samplers.reclaim_slot(deletion.slotIndex);
					break;
				case PendingType::kRtv:
					m_Rtvs.reclaim_slot(deletion.slotIndex);
					break;
				case PendingType::kDsv:
					m_Dsvs.reclaim_slot(deletion.slotIndex);
					break;
				}
			}

			return true;
		});
	}

	const Buffer&
	ResourceManager::GetBuffer(BufferHandle handle) const noexcept
	{
		gassert(m_Buffers.valid(handle.slot), "GetBuffer: invalid handle");
		return m_Buffers[handle.slot.index];
	}

	const wgpu::Buffer&
	ResourceManager::GetBufferBindingBySlotIndex(uint32_t slotIndex) const noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);
		return m_Buffers[slotIndex].GetHandle();
	}

	const wgpu::TextureView&
	ResourceManager::GetTextureBindingBySlotIndex(uint32_t slotIndex) const noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		const wgpu::TextureView& view = m_Textures[slotIndex].GetSampledView();
		gassert(view != nullptr, "Texture at slot {} was not created sampleable", slotIndex);
		return view;
	}

	const wgpu::Sampler&
	ResourceManager::GetSamplerBindingBySlotIndex(uint32_t slotIndex) const noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);
		return m_Samplers[slotIndex].GetHandle();
	}

	const ReadbackBuffer&
	ResourceManager::GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept
	{
		gassert(m_ReadbackBuffers.valid(handle.slot), "GetReadbackBuffer: invalid handle");
		return m_ReadbackBuffers[handle.slot.index];
	}

	const void*
	ResourceManager::MapReadback(ReadbackBufferHandle handle) noexcept
	{
		gassert(m_ReadbackBuffers.valid(handle.slot), "MapReadback: invalid handle");
		return m_ReadbackBuffers[handle.slot.index].Map();
	}

	void
	ResourceManager::UnmapReadback(ReadbackBufferHandle handle) noexcept
	{
		gassert(m_ReadbackBuffers.valid(handle.slot), "UnmapReadback: invalid handle");
		m_ReadbackBuffers[handle.slot.index].Unmap();
	}

	bool
	ResourceManager::ValidBufferHandle(const BufferHandle& handle) const noexcept
	{
		return m_Buffers.valid(handle.slot);
	}

	bool
	ResourceManager::ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept
	{
		return m_ReadbackBuffers.valid(handle.slot);
	}

	TextureHandle
	ResourceManager::CreateTexture(const TextureDesc& desc) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		const auto slot = m_Textures.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("ResourceManager: texture pool exhausted creating '{}'", desc.debugName);
			return TextureHandle{ core::slot_handle{} };
		}

		m_Textures[slot.index] = Texture(m_Device, desc);

		return TextureHandle{ slot, desc.usage };
	}

	SamplerHandle
	ResourceManager::CreateSampler(const SamplerDesc& desc) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		const auto slot = m_Samplers.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("CreateSampler: sampler pool exhausted");
			return SamplerHandle{};
		}

		m_Samplers[slot.index] = Sampler(m_Device, desc);

		return SamplerHandle{ slot.index, slot.generation };
	}

	RtvHandle
	ResourceManager::CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		gassert(m_Textures.valid(textureHandle.slot), "CreateRtv: invalid texture handle");

		const auto slot = m_Rtvs.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("CreateRtv: RTV pool exhausted creating '{}'", desc.debugName);
			return RtvHandle{};
		}

		auto viewDesc            = wgpu::TextureViewDescriptor{};
		viewDesc.label           = std::string_view(desc.debugName);
		viewDesc.format          = ToWgpuTextureFormat(desc.format);
		viewDesc.baseMipLevel    = desc.mipSlice;
		viewDesc.mipLevelCount   = 1;
		viewDesc.baseArrayLayer  = desc.firstArraySlice;
		viewDesc.arrayLayerCount = desc.arraySize;

		auto view = m_Textures[textureHandle.slot.index].GetHandle().CreateView(&viewDesc);

		m_Rtvs[slot.index] = Rtv(std::move(view), textureHandle, desc);

		return RtvHandle{ slot.index, slot.generation };
	}

	DsvHandle
	ResourceManager::CreateDsv(TextureHandle textureHandle, const DsvDesc& desc) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		gassert(m_Textures.valid(textureHandle.slot), "CreateDsv: invalid texture handle");

		const auto slot = m_Dsvs.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("CreateDsv: DSV pool exhausted creating '{}'", desc.debugName);
			return DsvHandle{};
		}

		auto viewDesc            = wgpu::TextureViewDescriptor{};
		viewDesc.label           = std::string_view(desc.debugName);
		viewDesc.format          = ToWgpuTextureFormat(desc.format);
		viewDesc.baseMipLevel    = desc.mipSlice;
		viewDesc.mipLevelCount   = 1;
		viewDesc.baseArrayLayer  = desc.firstArraySlice;
		viewDesc.arrayLayerCount = desc.arraySize;

		auto view = m_Textures[textureHandle.slot.index].GetHandle().CreateView(&viewDesc);

		m_Dsvs[slot.index] = Dsv(std::move(view), textureHandle, desc);

		return DsvHandle{ slot.index, slot.generation };
	}

	void
	ResourceManager::DestroyTexture(TextureHandle handle, bool deferred) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		if (!m_Textures.valid(handle.slot))
			return;

		if (deferred)
		{
			m_Textures.retire_slot(handle.slot);
			RetireDeferred(PendingType::kTexture, handle.slot.index);
			return;
		}

		m_Textures.release_slot(handle.slot);
	}

	void
	ResourceManager::DestroySampler(SamplerHandle handle, bool deferred) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		const auto slot = core::slot_handle{ handle.idx, handle.generation };
		if (!m_Samplers.valid(slot))
			return;

		if (deferred)
		{
			m_Samplers.retire_slot(slot);
			RetireDeferred(PendingType::kSampler, slot.index);
			return;
		}

		m_Samplers.release_slot(slot);
	}

	void
	ResourceManager::DestroyRtv(RtvHandle handle, bool deferred) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		const auto slot = core::slot_handle{ handle.idx, handle.generation };
		if (!m_Rtvs.valid(slot))
			return;

		if (deferred)
		{
			m_Rtvs.retire_slot(slot);
			RetireDeferred(PendingType::kRtv, slot.index);
			return;
		}

		m_Rtvs.release_slot(slot);
	}

	void
	ResourceManager::DestroyDsv(DsvHandle handle, bool deferred) noexcept
	{
		auto lock = std::scoped_lock(m_PoolMutex);

		const auto slot = core::slot_handle{ handle.idx, handle.generation };
		if (!m_Dsvs.valid(slot))
			return;

		if (deferred)
		{
			m_Dsvs.retire_slot(slot);
			RetireDeferred(PendingType::kDsv, slot.index);
			return;
		}

		m_Dsvs.release_slot(slot);
	}

	const Rtv&
	ResourceManager::GetRtv(RtvHandle handle) const noexcept
	{
		const auto slot = core::slot_handle{ handle.idx, handle.generation };
		gassert(m_Rtvs.valid(slot), "GetRtv: invalid handle");
		return m_Rtvs[slot.index];
	}

	const Dsv&
	ResourceManager::GetDsv(DsvHandle handle) const noexcept
	{
		const auto slot = core::slot_handle{ handle.idx, handle.generation };
		gassert(m_Dsvs.valid(slot), "GetDsv: invalid handle");
		return m_Dsvs[slot.index];
	}

	TextureHandle
	ResourceManager::GetRtvTexture(RtvHandle handle) const noexcept
	{
		return GetRtv(handle).GetTextureHandle();
	}

	TextureHandle
	ResourceManager::GetDsvTexture(DsvHandle handle) const noexcept
	{
		return GetDsv(handle).GetTextureHandle();
	}

	const Texture&
	ResourceManager::GetTexture(TextureHandle handle) const noexcept
	{
		gassert(m_Textures.valid(handle.slot), "GetTexture: invalid handle");
		return m_Textures[handle.slot.index];
	}

	TextureDesc
	ResourceManager::GetTextureDesc(TextureHandle handle) const noexcept
	{
		return GetTexture(handle).GetDesc();
	}

	const Sampler&
	ResourceManager::GetSampler(SamplerHandle handle) const noexcept
	{
		const auto slot = core::slot_handle{ handle.idx, handle.generation };
		gassert(m_Samplers.valid(slot), "GetSampler: invalid handle");
		return m_Samplers[slot.index];
	}

	TextureReadbackLayout
	ResourceManager::GetTextureReadbackLayout(TextureHandle handle) const noexcept
	{
		const TextureDesc& desc = GetTexture(handle).GetDesc();
		const FormatInfo&  info = GetFormatInfo(desc.format);

		// WebGPU requires bytesPerRow of a texture->buffer copy to be a multiple of 256.
		const uint64_t rowSize  = uint64_t{ desc.width } * info.bytesPerBlock;
		const uint64_t rowPitch = core::align(rowSize, 256);

		auto layout         = TextureReadbackLayout{};
		layout.offset       = 0;
		layout.rowPitch     = rowPitch;
		layout.rowSizeBytes = rowSize;
		layout.rowCount     = desc.height;
		layout.totalBytes   = rowPitch * desc.height;
		return layout;
	}

	bool
	ResourceManager::ValidTextureHandle(const TextureHandle& handle) const noexcept
	{
		return m_Textures.valid(handle.slot);
	}

	bool
	ResourceManager::IsTextureCube(const TextureHandle& handle) const noexcept
	{
		if (!ValidTextureHandle(handle))
			return false;

		const TextureDimension dim = GetTexture(handle).GetDesc().dimension;
		return dim == TextureDimension::kTextureCube || dim == TextureDimension::kTextureCubeArray;
	}

	bool
	ResourceManager::ValidSamplerHandle(const SamplerHandle& handle) const noexcept
	{
		return m_Samplers.valid(core::slot_handle{ handle.idx, handle.generation });
	}

	bool
	ResourceManager::ValidRtvHandle(const RtvHandle& handle) const noexcept
	{
		return m_Rtvs.valid(core::slot_handle{ handle.idx, handle.generation });
	}

	bool
	ResourceManager::ValidDsvHandle(const DsvHandle& handle) const noexcept
	{
		return m_Dsvs.valid(core::slot_handle{ handle.idx, handle.generation });
	}

	void
	ResourceManager::ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4]) noexcept
	{
		gassert(cmdList != nullptr, "ClearRtv: null command list");
		static_cast<CommandList*>(cmdList)->ClearRenderTarget(GetRtv(handle).GetView(), clearVal);
	}

	void
	ResourceManager::ClearDsv(
		ICommandList* cmdList,
		DsvHandle     handle,
		float         depth,
		uint8_t       stencil) noexcept
	{
		gassert(cmdList != nullptr, "ClearDsv: null command list");

		const Dsv& dsv        = GetDsv(handle);
		const bool hasStencil = GetFormatInfo(dsv.GetDesc().format).hasStencil;
		static_cast<CommandList*>(cmdList)
			->ClearDepthTarget(dsv.GetView(), depth, stencil, hasStencil);
	}
}
