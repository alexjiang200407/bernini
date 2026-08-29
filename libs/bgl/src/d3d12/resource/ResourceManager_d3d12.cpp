#include "d3d12/resource/ResourceManager_d3d12.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_d3d12.h"
#include "convert_d3d12.h"

namespace bgl
{
	ResourceManager::ResourceManager(
		wrl::ComPtr<ID3D12Device>  device,
		const ResourceManagerDesc& desc) :
		m_Device(std::move(device)), m_CbvSrvUavDescriptors(
										 m_Device.Get(),
										 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
										 desc.maxCbvSrvUavs,
										 D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE),
		m_Buffers(desc.maxBuffers), m_Srvs(desc.maxSrvs), m_Samplers(desc.maxSamplers),
		m_Textures(desc.maxTextures), m_ReadbackBuffers(desc.maxReadbackBuffers),
		m_Rtvs(desc.maxRtvs), m_Dsvs(desc.maxDsvs)
	{
		gassert(desc.maxBuffers > 0, "maxBuffers must be greater than zero");
		gassert(desc.maxSrvs > 0, "maxSrvs must be greater than zero");
		// Every buffer and every SRV takes a descriptor, so a heap smaller than the pools it serves
		// turns a legal create into an exhausted-heap failure.
		gassert(
			desc.maxCbvSrvUavs >= desc.maxBuffers + desc.maxSrvs,
			"maxCbvSrvUavs must cover maxBuffers + maxSrvs");
		gassert(desc.maxDsvs > 0, "maxDsvs must be greater than zero");
		gassert(desc.maxRtvs > 0, "maxRtvs must be greater than zero");
		gassert(desc.maxTextures > 0, "maxTextures must be greater than zero");
		gassert(desc.maxReadbackBuffers > 0, "maxReadbackBuffers must be greater than zero");

		gassert(
			desc.maxSamplers > 0 && desc.maxSamplers <= 2048,
			"maxSamplers must be in (0, 2048]");

		{
			D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
			rtvHeapDesc.NumDescriptors             = desc.maxRtvs;
			rtvHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtvHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RtvHeap)) >>
				d3d12ErrChecker;
		}

		{
			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
			dsvHeapDesc.NumDescriptors             = desc.maxDsvs;
			dsvHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			dsvHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			m_Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DsvHeap)) >>
				d3d12ErrChecker;
		}

		{
			D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
			samplerHeapDesc.NumDescriptors             = desc.maxSamplers;
			samplerHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			samplerHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			m_Device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_SamplerHeap)) >>
				d3d12ErrChecker;
		}
	}

	ResourceManager::BufferAllocation
	ResourceManager::AllocateBuffer(const BufferDesc& desc) noexcept
	{
		auto slot = m_Buffers.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("Creating buffer '{}': buffer pool exhausted", desc.debugName);
			return BufferAllocation{};
		}

		// Out of VRAM throws out of the Buffer ctor and a full heap throws out of Allocate, and this
		// is noexcept: catch here or a recoverable failure becomes a terminate.
		Buffer   buffer;
		uint32_t descriptorIndex = 0xFFFFFFFF;
		try
		{
			descriptorIndex = m_CbvSrvUavDescriptors.Allocate();
			buffer          = Buffer(
				m_Device.Get(),
				m_CbvSrvUavDescriptors.GetD3D12Heap(),
				descriptorIndex,
				desc);
		}
		catch (const std::exception& e)
		{
			logger::error(
				"Creating buffer '{}': allocating {} bytes of device memory failed: {}",
				desc.debugName,
				desc.byteSize,
				e.what());
			if (descriptorIndex != 0xFFFFFFFF)
			{
				m_CbvSrvUavDescriptors.Free(descriptorIndex);
			}
			m_Buffers.release_slot(slot.index);
			return BufferAllocation{};
		}

		BufferAllocation allocation;
		allocation.slot            = slot;
		allocation.descriptorIndex = descriptorIndex;
		allocation.buffer          = std::move(buffer);
		return allocation;
	}

	BufferHandle
	ResourceManager::CreateStructBuffer(const StructBufferDesc& desc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(desc.stride > 0, "StructuredBuffer requires a valid structural stride");
		gassert(desc.elementCount > 0, "StructuredBuffer requires a valid element count");

		BufferDesc bufferDesc;
		bufferDesc.byteSize  = static_cast<uint64_t>(desc.stride) * desc.elementCount;
		bufferDesc.isUav     = desc.isUav;
		bufferDesc.debugName = desc.debugName;

		auto allocation = AllocateBuffer(bufferDesc);
		if (allocation.slot.is_null())
		{
			return BufferHandle{};
		}

		auto& buffer = allocation.buffer;

		if (desc.isUav)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Format                           = DXGI_FORMAT_UNKNOWN;
			uavDesc.Buffer.FirstElement              = 0;
			uavDesc.Buffer.NumElements               = desc.elementCount;
			uavDesc.Buffer.StructureByteStride       = desc.stride;
			uavDesc.Buffer.Flags                     = D3D12_BUFFER_UAV_FLAG_NONE;

			m_Device->CreateUnorderedAccessView(
				buffer.GetD3D12Resource(),
				nullptr,
				&uavDesc,
				buffer.GetCpuHandle());
		}
		else
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format                          = DXGI_FORMAT_UNKNOWN;
			srvDesc.Buffer.FirstElement             = 0;
			srvDesc.Buffer.NumElements              = desc.elementCount;
			srvDesc.Buffer.StructureByteStride      = desc.stride;
			srvDesc.Buffer.Flags                    = D3D12_BUFFER_SRV_FLAG_NONE;

			m_Device->CreateShaderResourceView(
				buffer.GetD3D12Resource(),
				&srvDesc,
				buffer.GetCpuHandle());
		}

		m_Buffers[allocation.slot] = std::move(buffer);

		return BufferHandle{ allocation.slot, allocation.descriptorIndex };
	}

	BufferHandle
	ResourceManager::CreateRawBuffer(const RawBufferDesc& desc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(desc.byteSize > 0, "A raw buffer requires a byte size");
		gassert(desc.byteSize % 4 == 0, "A raw view addresses whole 32-bit words");
		gassert(desc.byteSize <= c_MaxRawBufferBytes, "A raw view cannot address past 4 GiB");

		BufferDesc bufferDesc;
		bufferDesc.byteSize  = desc.byteSize;
		bufferDesc.isUav     = desc.isUav;
		bufferDesc.isRaw     = true;
		bufferDesc.debugName = desc.debugName;

		auto allocation = AllocateBuffer(bufferDesc);
		if (allocation.slot.is_null())
		{
			return BufferHandle{};
		}

		auto& buffer = allocation.buffer;

		// A raw view counts 32-bit words, where a structured one counts elements of its stride.
		const auto wordCount = static_cast<uint32_t>(desc.byteSize / 4);

		if (desc.isUav)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Format                           = DXGI_FORMAT_R32_TYPELESS;
			uavDesc.Buffer.FirstElement              = 0;
			uavDesc.Buffer.NumElements               = wordCount;
			uavDesc.Buffer.StructureByteStride       = 0;
			uavDesc.Buffer.Flags                     = D3D12_BUFFER_UAV_FLAG_RAW;

			m_Device->CreateUnorderedAccessView(
				buffer.GetD3D12Resource(),
				nullptr,
				&uavDesc,
				buffer.GetCpuHandle());
		}
		else
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format                          = DXGI_FORMAT_R32_TYPELESS;
			srvDesc.Buffer.FirstElement             = 0;
			srvDesc.Buffer.NumElements              = wordCount;
			srvDesc.Buffer.StructureByteStride      = 0;
			srvDesc.Buffer.Flags                    = D3D12_BUFFER_SRV_FLAG_RAW;

			m_Device->CreateShaderResourceView(
				buffer.GetD3D12Resource(),
				&srvDesc,
				buffer.GetCpuHandle());
		}

		m_Buffers[allocation.slot] = std::move(buffer);

		return BufferHandle{ allocation.slot, allocation.descriptorIndex };
	}

	BufferHandle
	ResourceManager::CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept
	{
		gassert(desc.initialCount > 0, "ComputeBuffer requires a positive element count");
		gassert(desc.elementSize > 0, "ComputeBuffer requires a positive element size");

		// A compute buffer is a GPU-only structured buffer with UAV access; reuse the
		// structured-buffer path (default heap + UAV) to create it.
		StructBufferDesc structDesc;
		structDesc.stride       = desc.elementSize;
		structDesc.elementCount = desc.initialCount;
		structDesc.isUav        = true;
		structDesc.debugName    = desc.debugName;

		return CreateStructBuffer(structDesc);
	}

	TextureHandle
	ResourceManager::CreateTexture(const TextureDesc& desc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		auto                        slot = m_Textures.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("CreateTexture '{}': texture pool exhausted", desc.debugName);
			return TextureHandle{};
		}
		m_Textures[slot.index] = Texture(m_Device.Get(), nullptr, slot.index, desc);
		// A texture has no descriptor. CreateSrv is what makes one shader-visible.
		return TextureHandle{ slot };
	}

	SamplerHandle
	ResourceManager::CreateSampler(const SamplerDesc& desc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		auto                        samplerSlotHandle = m_Samplers.try_allocate_slot();
		if (samplerSlotHandle.is_null())
		{
			logger::error("CreateSampler: sampler pool exhausted");
			return SamplerHandle{};
		}
		uint32_t slotIndex = samplerSlotHandle.index;

		Sampler sampler(m_Device.Get(), m_SamplerHeap.Get(), slotIndex, desc);

		D3D12_SAMPLER_DESC d3d12Desc = ConvertSamplerDesc(desc);
		m_Device->CreateSampler(&d3d12Desc, sampler.GetCpuHandle());

		m_Samplers[slotIndex] = std::move(sampler);

		return SamplerHandle{ slotIndex, samplerSlotHandle.generation, slotIndex };
	}

	ReadbackBufferHandle
	ResourceManager::CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(desc.byteSize > 0, "Readback buffer requires a positive byte size");

		auto slot = m_ReadbackBuffers.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("CreateReadbackBuffer: readback pool exhausted");
			return ReadbackBufferHandle{};
		}
		uint32_t slotIndex = slot.index;

		m_ReadbackBuffers[slotIndex] = ReadbackBuffer(m_Device.Get(), desc);

		return ReadbackBufferHandle{ slot };
	}

	TextureHandle
	ResourceManager::CreateTexture(
		wrl::ComPtr<ID3D12Resource> d3d12Texture,
		const TextureDesc&          desc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		auto                        slot = m_Textures.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("CreateTexture '{}': texture pool exhausted", desc.debugName);
			return TextureHandle{};
		}
		m_Textures[slot.index] =
			Texture(m_Device.Get(), nullptr, slot.index, std::move(d3d12Texture), desc);
		// A texture has no descriptor. CreateSrv is what makes one shader-visible.
		return TextureHandle{ slot };
	}

	SrvHandle
	ResourceManager::CreateSrv(TextureHandle textureHandle, const SrvDesc& desc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidTextureHandle(textureHandle), "CreateSrv on an invalid texture");

		auto slot = m_Srvs.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("CreateSrv '{}': SRV pool exhausted", desc.debugName);
			return SrvHandle{};
		}

		uint32_t descriptorIndex = 0xFFFFFFFF;
		try
		{
			descriptorIndex = m_CbvSrvUavDescriptors.Allocate();
		}
		catch (const std::exception& e)
		{
			logger::error("CreateSrv '{}': {}", desc.debugName, e.what());
			m_Srvs.release_slot(slot.index);
			return SrvHandle{};
		}

		m_Srvs[slot.index] =
			Srv(m_Device.Get(),
		        textureHandle,
		        m_Textures[textureHandle.slot].GetD3D12Resource(),
		        m_CbvSrvUavDescriptors.GetD3D12Heap(),
		        descriptorIndex,
		        desc);

		// The heap index is both what a cbuffer carries and what GPU memory carries: a D3D12 shader
		// indexes the same heap either way. It is no longer the slot.
		return SrvHandle{ slot.index,
			              slot.generation,
			              descriptorIndex,
			              DescriptorHandle(descriptorIndex) };
	}

	RtvHandle
	ResourceManager::CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		auto&                       texture       = GetTexture(textureHandle);
		wrl::ComPtr<ID3D12Resource> resource      = texture.GetD3D12ResourceCopy();
		auto                        rtvSlotHandle = m_Rtvs.try_allocate_slot();
		if (rtvSlotHandle.is_null())
		{
			logger::error("CreateRtv: RTV pool exhausted");
			return RtvHandle{};
		}

		uint32_t descriptorIndex = rtvSlotHandle.index;
		Rtv      rtv(m_Device.Get(), textureHandle, m_RtvHeap.Get(), descriptorIndex, desc);

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format        = ConvertFormat(desc.format);
		rtvDesc.ViewDimension = ConvertRTVDimension(desc.dimension);

		switch (rtvDesc.ViewDimension)
		{
		case D3D12_RTV_DIMENSION_TEXTURE1D:
			rtvDesc.Texture1D.MipSlice = desc.mipSlice;
			break;
		case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
			rtvDesc.Texture1DArray.MipSlice        = desc.mipSlice;
			rtvDesc.Texture1DArray.FirstArraySlice = desc.firstArraySlice;
			rtvDesc.Texture1DArray.ArraySize       = desc.arraySize;
			break;
		case D3D12_RTV_DIMENSION_TEXTURE2D:
			rtvDesc.Texture2D.MipSlice   = desc.mipSlice;
			rtvDesc.Texture2D.PlaneSlice = desc.planeSlice;
			break;
		case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
			rtvDesc.Texture2DArray.MipSlice        = desc.mipSlice;
			rtvDesc.Texture2DArray.FirstArraySlice = desc.firstArraySlice;
			rtvDesc.Texture2DArray.ArraySize       = desc.arraySize;
			rtvDesc.Texture2DArray.PlaneSlice      = desc.planeSlice;
			break;
		case D3D12_RTV_DIMENSION_TEXTURE2DMS:
			// No additional fields to set
			break;
		case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY:
			rtvDesc.Texture2DMSArray.FirstArraySlice = desc.firstArraySlice;
			rtvDesc.Texture2DMSArray.ArraySize       = desc.arraySize;
			break;
		case D3D12_RTV_DIMENSION_TEXTURE3D:
			rtvDesc.Texture3D.MipSlice    = desc.mipSlice;
			rtvDesc.Texture3D.FirstWSlice = desc.firstWSlice;
			rtvDesc.Texture3D.WSize       = desc.wSize;
			break;
		case D3D12_RTV_DIMENSION_UNKNOWN:
		case D3D12_RTV_DIMENSION_BUFFER:
		default:
			gfatal("Unsupported RTV dimension");
		}

		if (!desc.debugName.empty())
		{
			resource->SetName(std::wstring(desc.debugName.begin(), desc.debugName.end()).c_str());
		}

		m_Device->CreateRenderTargetView(resource.Get(), &rtvDesc, rtv.GetCpuHandle());

		m_Rtvs[descriptorIndex] = std::move(rtv);

		return RtvHandle{ descriptorIndex, rtvSlotHandle.generation };
	}

	void
	ResourceManager::DestroyRtv(RtvHandle handle, bool deferred) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidRtvHandle(handle), "Cannot destroy invalid RTV handle");

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

	void
	ResourceManager::DestroyBuffer(BufferHandle handle, bool deferred) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidBufferHandle(handle), "Cannot destroy invalid buffer handle");

		// Read from the record rather than the handle: the manager allocated this descriptor and is
		// the only thing that knows which one it is.
		const uint32_t descriptorIndex = m_Buffers[handle.slot].GetDescriptorIndex();

		if (deferred)
		{
			m_Buffers.retire_slot(handle.slot);
			RetireDeferred(PendingType::kBuffer, handle.slot.index, descriptorIndex);
		}
		else
		{
			m_Buffers.release_slot(handle.slot);
			m_CbvSrvUavDescriptors.Free(descriptorIndex);
		}
	}

	void
	ResourceManager::DestroyTexture(TextureHandle handle, bool deferred) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidTextureHandle(handle), "Cannot destroy invalid texture handle");

		// Views onto this texture are not cascaded to; release them separately, as with an Rtv.
		if (deferred)
		{
			// Retiring now stales every handle immediately; the resource survives until the sweep
			// reclaims it.
			m_Textures.retire_slot(handle.slot);
			RetireDeferred(PendingType::kTexture, handle.slot.index);
		}
		else
		{
			m_Textures.release_slot(handle.slot);
		}
	}

	void
	ResourceManager::DestroySrv(SrvHandle handle, bool deferred) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidSrvHandle(handle), "Cannot destroy invalid SRV handle");

		const uint32_t descriptorIndex = m_Srvs[handle.idx].GetDescriptorIndex();

		// An SRV owns no allocation -- only its descriptor, which outlives in-flight work exactly as
		// a resource does, so the deferred path hands it back on the same gate.
		if (deferred)
		{
			m_Srvs.retire_slot(handle.idx);
			RetireDeferred(PendingType::kSrv, handle.idx, descriptorIndex);
		}
		else
		{
			m_Srvs.release_slot(handle.idx);
			m_CbvSrvUavDescriptors.Free(descriptorIndex);
		}
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

	void
	ResourceManager::DestroyReadbackBuffer(ReadbackBufferHandle handle, bool deferred) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidReadbackBufferHandle(handle), "Cannot destroy invalid readback buffer handle");

		if (deferred)
		{
			m_ReadbackBuffers.retire_slot(handle.slot.index);
			RetireDeferred(PendingType::kReadback, handle.slot.index);
		}
		else
		{
			m_ReadbackBuffers.release_slot(handle.slot.index);
		}
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
		// pending free. Drop it from every batch's gate so no gate outlives the queue's registration
		// -- otherwise a freed queue's pointer would linger in a gate and, if its address were reused
		// by a later queue, alias it. A batch left with an empty gate is reclaimed on the next
		// cleanup. Each queue appears at most once per gate.
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
		{
			gate.push_back({ queue, queue->GetNextFenceValue() });
		}
		return gate;
	}

	void
	ResourceManager::RetireDeferred(
		PendingType type,
		uint32_t    slotIndex,
		uint32_t    descriptorIndex) noexcept
	{
		const DeletionGate gate = CaptureGate();

		// Coalesce with the most recent batch when the gate has not moved -- fences only advance, so
		// consecutive destroys within a frame share one gate rather than each storing a copy.
		if (m_PendingBatches.empty() || !std::ranges::equal(m_PendingBatches.back().gate, gate))
		{
			m_PendingBatches.push_back({ gate, {} });
		}
		m_PendingBatches.back().deletions.push_back({ type, slotIndex, descriptorIndex });
	}

	void
	ResourceManager::CleanupExpiredResources() noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		// Poll each queue once, not per pending deletion.
		auto completed = core::static_vector<QueueGate, c_MaxRegisteredQueues>();
		for (ICommandQueue* queue : m_RegisteredQueues)
		{
			completed.push_back({ queue, queue->PollCurrentFenceValue() });
		}

		const auto isCleared = [&](const QueueGate& entry) {
			for (const auto& [queue, value] : completed)
			{
				if (queue == entry.queue)
				{
					return value >= entry.fenceValue;
				}
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
				if (pending.descriptorIndex != 0xFFFFFFFF)
				{
					m_CbvSrvUavDescriptors.Free(pending.descriptorIndex);
				}
				break;
			case PendingType::kSrv:
				m_Srvs.reclaim_slot(pending.slotIndex);
				if (pending.descriptorIndex != 0xFFFFFFFF)
				{
					m_CbvSrvUavDescriptors.Free(pending.descriptorIndex);
				}
				break;
			case PendingType::kRtv:
				m_Rtvs.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kDsv:
				m_Dsvs.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kTexture:
				m_Textures.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kReadback:
				m_ReadbackBuffers.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kSampler:
				m_Samplers.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kInvalid:
				gassert(false, "A pending deletion was recorded with no resource type");
				break;
			}
		};

		// A batch's slots were retired when they were recorded, so their handles have been stale
		// since then. Freeing the batch reclaims each index into its pool's free list.
		std::erase_if(m_PendingBatches, [&](const PendingDeletionBatch& batch) {
			if (!std::ranges::all_of(batch.gate, isCleared))
			{
				return false;
			}
			for (const PendingDeletion& pending : batch.deletions)
			{
				reclaim(pending);
			}
			return true;
		});
	}

	bool
	ResourceManager::ValidBufferHandle(const BufferHandle& handle) const noexcept
	{
		return m_Buffers.valid(handle.slot) && !m_Buffers[handle.slot].IsNull();
	}

	bool
	ResourceManager::ValidTextureHandle(const TextureHandle& handle) const noexcept
	{
		if (!m_Textures.valid(handle.slot))
		{
			return false;
		}

		return !m_Textures[handle.slot].IsNull();
	}

	bool
	ResourceManager::IsTextureCube(const TextureHandle& handle) const noexcept
	{
		if (!ValidTextureHandle(handle))
		{
			return false;
		}

		const TextureDimension dim = GetTexture(handle).GetDesc().dimension;
		return dim == TextureDimension::kTextureCube || dim == TextureDimension::kTextureCubeArray;
	}

	bool
	ResourceManager::ValidSamplerHandle(const SamplerHandle& handle) const noexcept
	{
		if (!m_Samplers.valid(handle.idx, handle.generation))
		{
			return false;
		}

		return !m_Samplers[handle.idx].IsNull();
	}

	bool
	ResourceManager::ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept
	{
		if (!m_ReadbackBuffers.valid(handle.slot))
		{
			return false;
		}

		return !m_ReadbackBuffers[handle.slot.index].IsNull();
	}

	bool
	ResourceManager::ValidRtvHandle(const RtvHandle& handle) const noexcept
	{
		if (!m_Rtvs.valid(handle.idx, handle.generation))
		{
			return false;
		}

		return !m_Rtvs[handle.idx].IsNull();
	}

	void
	ResourceManager::SetDescriptorHeap(ID3D12GraphicsCommandList* cmdList) noexcept
	{
		gassert(cmdList != nullptr, "Command list cannot be null");
		ID3D12DescriptorHeap* heaps[] = { m_CbvSrvUavDescriptors.GetD3D12Heap(),
			                              m_SamplerHeap.Get() };
		cmdList->SetDescriptorHeaps(_countof(heaps), heaps);
	}

	bool
	ResourceManager::ValidSrvHandle(const SrvHandle& handle) const noexcept
	{
		return !handle.IsNull() && m_Srvs.valid(handle.idx, handle.generation) &&
		       !m_Srvs[handle.idx].IsNull();
	}

	const Texture&
	ResourceManager::GetTexture(TextureHandle handle) const noexcept
	{
		gassert(ValidTextureHandle(handle), "Invalid texture handle");
		return m_Textures[handle.slot];
	}

	TextureDesc
	ResourceManager::GetTextureDesc(TextureHandle handle) const noexcept
	{
		return GetTexture(handle).GetDesc();
	}

	const Sampler&
	ResourceManager::GetSampler(SamplerHandle handle) const noexcept
	{
		gassert(ValidSamplerHandle(handle), "Invalid sampler handle");
		return m_Samplers[handle.idx];
	}

	const Buffer&
	ResourceManager::GetBuffer(BufferHandle handle) const noexcept
	{
		gassert(ValidBufferHandle(handle), "Invalid buffer handle");
		return m_Buffers[handle.slot];
	}

	BufferDesc
	ResourceManager::GetBufferDesc(BufferHandle handle) const noexcept
	{
		return GetBuffer(handle).GetDesc();
	}

	const ReadbackBuffer&
	ResourceManager::GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept
	{
		gassert(ValidReadbackBufferHandle(handle), "Invalid readback buffer handle");
		return m_ReadbackBuffers[handle.slot.index];
	}

	TextureReadbackLayout
	ResourceManager::GetTextureReadbackLayout(TextureHandle handle) const noexcept
	{
		const auto&         texture     = GetTexture(handle);
		D3D12_RESOURCE_DESC textureDesc = texture.GetD3D12Resource()->GetDesc();

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint    = {};
		UINT                               rowCount     = 0;
		UINT64                             rowSizeBytes = 0;
		UINT64                             totalBytes   = 0;

		m_Device->GetCopyableFootprints(
			&textureDesc,
			0,
			1,
			0,
			&footprint,
			&rowCount,
			&rowSizeBytes,
			&totalBytes);

		TextureReadbackLayout layout;
		layout.offset       = footprint.Offset;
		layout.rowPitch     = footprint.Footprint.RowPitch;
		layout.rowSizeBytes = rowSizeBytes;
		layout.rowCount     = rowCount;
		layout.totalBytes   = totalBytes;
		return layout;
	}

	const void*
	ResourceManager::MapReadback(ReadbackBufferHandle handle) noexcept
	{
		gassert(ValidReadbackBufferHandle(handle), "Invalid readback buffer handle");
		return m_ReadbackBuffers[handle.slot.index].Map();
	}

	void
	ResourceManager::UnmapReadback(ReadbackBufferHandle handle) noexcept
	{
		gassert(ValidReadbackBufferHandle(handle), "Invalid readback buffer handle");
		m_ReadbackBuffers[handle.slot.index].Unmap();
	}

	const Rtv&
	ResourceManager::GetRtv(RtvHandle handle) const noexcept
	{
		gassert(ValidRtvHandle(handle), "Invalid RTV handle");
		return m_Rtvs[handle.idx];
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

	void
	ResourceManager::ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4]) noexcept
	{
		gassert(cmdList != nullptr, "Command list cannot be null");
		gassert(ValidRtvHandle(handle), "Handle is Null");
		gassert(cmdList->IsOpen(), "Command list must be open to clear texture");
		gassert(
			cmdList->GetType() == QueueType::kGraphics,
			"ClearTexture can only be called on graphics command list");

		auto d3d12GfxCmdList = wrl::ComPtr<ID3D12GraphicsCommandList>();
		cmdList->As<CommandList>()->GetD3D12CommandList()->QueryInterface(
			IID_PPV_ARGS(&d3d12GfxCmdList)) >>
			d3d12ErrChecker;

		auto& rtv = GetRtv(handle);
		d3d12GfxCmdList->ClearRenderTargetView(rtv.GetCpuHandle(), clearVal, 0, nullptr);
	}

	DsvHandle
	ResourceManager::CreateDsv(TextureHandle textureHandle, const DsvDesc& desc) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		auto&                       texture       = GetTexture(textureHandle);
		wrl::ComPtr<ID3D12Resource> resource      = texture.GetD3D12ResourceCopy();
		auto                        dsvSlotHandle = m_Dsvs.try_allocate_slot();
		if (dsvSlotHandle.is_null())
		{
			logger::error("CreateDsv: DSV pool exhausted");
			return DsvHandle{};
		}

		uint32_t descriptorIndex = dsvSlotHandle.index;
		Dsv      dsv(m_Device.Get(), textureHandle, m_DsvHeap.Get(), descriptorIndex, desc);

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format        = ConvertFormat(desc.format);
		dsvDesc.ViewDimension = ConvertDSVDimension(desc.dimension);

		switch (dsvDesc.ViewDimension)
		{
		case D3D12_DSV_DIMENSION_TEXTURE1D:
			dsvDesc.Texture1D.MipSlice = desc.mipSlice;
			break;

		case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
			dsvDesc.Texture1DArray.MipSlice        = desc.mipSlice;
			dsvDesc.Texture1DArray.FirstArraySlice = desc.firstArraySlice;
			dsvDesc.Texture1DArray.ArraySize       = desc.arraySize;
			break;

		case D3D12_DSV_DIMENSION_TEXTURE2D:
			dsvDesc.Texture2D.MipSlice = desc.mipSlice;
			break;

		case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
			dsvDesc.Texture2DArray.MipSlice        = desc.mipSlice;
			dsvDesc.Texture2DArray.FirstArraySlice = desc.firstArraySlice;
			dsvDesc.Texture2DArray.ArraySize       = desc.arraySize;
			break;

		case D3D12_DSV_DIMENSION_TEXTURE2DMS:
			// Multi-sampled views have no explicit subresource fields to populate
			break;

		case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY:
			dsvDesc.Texture2DMSArray.FirstArraySlice = desc.firstArraySlice;
			dsvDesc.Texture2DMSArray.ArraySize       = desc.arraySize;
			break;

		case D3D12_DSV_DIMENSION_UNKNOWN:
		default:
			gfatal("Unsupported DSV dimension passed to configuration switch");
		}

		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

		if (!desc.debugName.empty())
		{
			resource->SetName(std::wstring(desc.debugName.begin(), desc.debugName.end()).c_str());
		}

		m_Device->CreateDepthStencilView(resource.Get(), &dsvDesc, dsv.GetCpuHandle());

		m_Dsvs[descriptorIndex] = std::move(dsv);

		return DsvHandle{ descriptorIndex, dsvSlotHandle.generation };
	}

	const Dsv&
	ResourceManager::GetDsv(DsvHandle handle) const noexcept
	{
		gassert(ValidDsvHandle(handle), "Invalid RTV handle");
		return m_Dsvs[handle.idx];
	}

	bool
	ResourceManager::ValidDsvHandle(const DsvHandle& handle) const noexcept
	{
		if (!m_Dsvs.valid(handle.idx, handle.generation))
		{
			return false;
		}

		return !m_Dsvs[handle.idx].IsNull();
	}

	void
	ResourceManager::ClearDsv(
		ICommandList* cmdList,
		DsvHandle     handle,
		float         depth,
		uint8_t       stencil) noexcept
	{
		gassert(cmdList != nullptr, "Command list cannot be null");
		gassert(ValidDsvHandle(handle), "Handle is Null");
		gassert(cmdList->IsOpen(), "Command list must be open to clear texture");
		gassert(
			cmdList->GetType() == QueueType::kGraphics,
			"ClearTexture can only be called on graphics command list");

		auto d3d12GfxCmdList = wrl::ComPtr<ID3D12GraphicsCommandList>();
		cmdList->As<CommandList>()->GetD3D12CommandList()->QueryInterface(
			IID_PPV_ARGS(&d3d12GfxCmdList)) >>
			d3d12ErrChecker;

		D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
		auto&             dsv   = GetDsv(handle);

		d3d12GfxCmdList
			->ClearDepthStencilView(dsv.GetCpuHandle(), flags, depth, stencil, 0, nullptr);
	}

	void
	ResourceManager::DestroyDsv(DsvHandle handle, bool deferred) noexcept
	{
		std::lock_guard<std::mutex> lock(m_PoolMutex);
		gassert(ValidDsvHandle(handle), "Cannot destroy invalid RTV handle");

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
}
