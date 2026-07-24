#include "resource/ResourceManager_wgpu.h"

#include "cmd/CommandQueue.h"

namespace bgl
{
	ResourceManager::ResourceManager(
		WGPUDevice                 device,
		WGPUInstance               instance,
		const ResourceManagerDesc& desc) :
		m_Device(device), m_Instance(instance), m_Buffers(desc.maxCbvSrvUavs),
		m_ReadbackBuffers(desc.maxReadbackBuffers)
	{
		gassert(m_Device != nullptr, "ResourceManager: null device");

		wgpuDeviceAddRef(m_Device);
		wgpuInstanceAddRef(m_Instance);
	}

	ResourceManager::~ResourceManager() noexcept
	{
		wgpuInstanceRelease(m_Instance);
		wgpuDeviceRelease(m_Device);
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
		return CreateStructBuffer(
			StructBufferDesc{}
				.SetElementCount(desc.maxCount)
				.SetIsUav(true)
				.SetDebugName(desc.debugName));
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

		if (std::ranges::find(m_Queues, queue) == m_Queues.end())
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

	// --- not implemented yet -------------------------------------------------------------------
	// Textures, samplers and views land with the raster path; the handle families exist so the
	// interface is satisfied, and every entry point fails loudly rather than returning something
	// a caller would go on to use.

	TextureHandle
	ResourceManager::CreateTexture(const TextureDesc&) noexcept
	{
		gfatal("CreateTexture: textures are not implemented on the WebGPU backend yet");
	}

	SamplerHandle
	ResourceManager::CreateSampler(const SamplerDesc&) noexcept
	{
		gfatal("CreateSampler: samplers are not implemented on the WebGPU backend yet");
	}

	RtvHandle
	ResourceManager::CreateRtv(TextureHandle, const RtvDesc&) noexcept
	{
		gfatal("CreateRtv: render target views are not implemented on the WebGPU backend yet");
	}

	DsvHandle
	ResourceManager::CreateDsv(TextureHandle, const DsvDesc&) noexcept
	{
		gfatal("CreateDsv: depth stencil views are not implemented on the WebGPU backend yet");
	}

	void
	ResourceManager::DestroyTexture(TextureHandle, bool) noexcept
	{
		gfatal("DestroyTexture: textures are not implemented on the WebGPU backend yet");
	}

	void
	ResourceManager::DestroySampler(SamplerHandle, bool) noexcept
	{
		gfatal("DestroySampler: samplers are not implemented on the WebGPU backend yet");
	}

	void
	ResourceManager::DestroyRtv(RtvHandle, bool) noexcept
	{
		gfatal("DestroyRtv: render target views are not implemented on the WebGPU backend yet");
	}

	void
	ResourceManager::DestroyDsv(DsvHandle, bool) noexcept
	{
		gfatal("DestroyDsv: depth stencil views are not implemented on the WebGPU backend yet");
	}

	const Rtv&
	ResourceManager::GetRtv(RtvHandle) const noexcept
	{
		gfatal("GetRtv: render target views are not implemented on the WebGPU backend yet");
	}

	const Dsv&
	ResourceManager::GetDsv(DsvHandle) const noexcept
	{
		gfatal("GetDsv: depth stencil views are not implemented on the WebGPU backend yet");
	}

	TextureHandle
	ResourceManager::GetRtvTexture(RtvHandle) const noexcept
	{
		gfatal("GetRtvTexture: render target views are not implemented on the WebGPU backend yet");
	}

	TextureHandle
	ResourceManager::GetDsvTexture(DsvHandle) const noexcept
	{
		gfatal("GetDsvTexture: depth stencil views are not implemented on the WebGPU backend yet");
	}

	const Texture&
	ResourceManager::GetTexture(TextureHandle) const noexcept
	{
		gfatal("GetTexture: textures are not implemented on the WebGPU backend yet");
	}

	TextureDesc
	ResourceManager::GetTextureDesc(TextureHandle) const noexcept
	{
		gfatal("GetTextureDesc: textures are not implemented on the WebGPU backend yet");
	}

	const Sampler&
	ResourceManager::GetSampler(SamplerHandle) const noexcept
	{
		gfatal("GetSampler: samplers are not implemented on the WebGPU backend yet");
	}

	TextureReadbackLayout
	ResourceManager::GetTextureReadbackLayout(TextureHandle) const noexcept
	{
		gfatal("GetTextureReadbackLayout: textures are not implemented on the WebGPU backend yet");
	}

	bool
	ResourceManager::ValidTextureHandle(const TextureHandle&) const noexcept
	{
		return false;
	}

	bool
	ResourceManager::IsTextureCube(const TextureHandle&) const noexcept
	{
		return false;
	}

	bool
	ResourceManager::ValidSamplerHandle(const SamplerHandle&) const noexcept
	{
		return false;
	}

	bool
	ResourceManager::ValidRtvHandle(const RtvHandle&) const noexcept
	{
		return false;
	}

	bool
	ResourceManager::ValidDsvHandle(const DsvHandle&) const noexcept
	{
		return false;
	}

	void
	ResourceManager::ClearRtv(ICommandList*, RtvHandle, float[4]) noexcept
	{
		gfatal("ClearRtv: render target views are not implemented on the WebGPU backend yet");
	}

	void
	ResourceManager::ClearDsv(ICommandList*, DsvHandle, float, uint8_t) noexcept
	{
		gfatal("ClearDsv: depth stencil views are not implemented on the WebGPU backend yet");
	}
}
