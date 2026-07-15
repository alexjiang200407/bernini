#include "d3d12/resource/ResourceManager_d3d12.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_d3d12.h"
#include "util_d3d12.h"

namespace bgl
{
	ResourceManager::ResourceManager(
		wrl::ComPtr<ID3D12Device>  device,
		const ResourceManagerDesc& desc,
		CommandQueueHandle         submissionQueue) :
		m_Desc(desc), m_Device(std::move(device)), m_CbvSrvUavSlots(desc.maxCbvSrvUavs),
		m_Samplers(desc.maxSamplers), m_Textures(desc.maxTextures), m_Rtvs(desc.maxRtvs),
		m_Dsvs(desc.maxDsvs), m_SubmissionQueue(std::move(submissionQueue))
	{
		gassert(m_SubmissionQueue != nullptr, "ResourceManager requires a submission queue");
		gassert(desc.maxCbvSrvUavs > 0, "maxDescriptors must be greater than zero");
		gassert(desc.maxDsvs > 0, "maxDsvs must be greater than zero");
		gassert(desc.maxRtvs > 0, "maxRtvs must be greater than zero");
		gassert(desc.maxTextures > 0, "maxTextures must be greater than zero");

		gassert(
			desc.maxSamplers > 0 && desc.maxSamplers <= 2048,
			"maxSamplers must be in (0, 2048]");

		// Create descriptor heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
			heapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDesc.NumDescriptors             = desc.maxCbvSrvUavs;
			heapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			m_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_CbvSrvUavHeap)) >>
				d3d12ErrChecker;
		}

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

	BufferHandle
	ResourceManager::CreateStructBuffer(const StructBufferDesc& desc) noexcept
	{
		gassert(desc.stride > 0, "StructuredBuffer requires a valid structural stride");
		gassert(desc.elementCount > 0, "StructuredBuffer requires a valid element count");

		auto bufferSlotHandle = m_CbvSrvUavSlots.try_allocate_slot();
		if (bufferSlotHandle.is_null())
		{
			logger::error("CreateStructBuffer '{}': CBV/SRV/UAV pool exhausted", desc.debugName);
			return BufferHandle{};
		}

		BufferDesc bufferDesc;
		bufferDesc.byteSize  = static_cast<uint64_t>(desc.stride) * desc.elementCount;
		bufferDesc.isUav     = desc.isUav;
		bufferDesc.debugName = desc.debugName;

		Buffer buffer(m_Device.Get(), m_CbvSrvUavHeap.Get(), bufferSlotHandle.index, bufferDesc);

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

		m_CbvSrvUavSlots[bufferSlotHandle] = std::move(buffer);

		return BufferHandle{ bufferSlotHandle };
	}

	BufferHandle
	ResourceManager::CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept
	{
		gassert(desc.maxCount > 0, "ComputeBuffer requires a positive element count");
		gassert(desc.elementSize > 0, "ComputeBuffer requires a positive element size");

		// A compute buffer is a GPU-only structured buffer with UAV access; reuse the
		// structured-buffer path (default heap + UAV) to create it.
		StructBufferDesc structDesc;
		structDesc.stride       = desc.elementSize;
		structDesc.elementCount = desc.maxCount;
		structDesc.isUav        = true;
		structDesc.debugName    = desc.debugName;

		return CreateStructBuffer(structDesc);
	}

	TextureHandle
	ResourceManager::CreateTexture(const TextureDesc& desc) noexcept
	{
		// Sampled textures go in the shader-visible pool so the slot index is a valid
		// bindless SRV index. RTV/DSV-only textures go in m_Textures and never take a
		// shader-visible descriptor slot.
		if (desc.usage.any(TextureUsageFlag::kSRV))
		{
			auto slot = m_CbvSrvUavSlots.try_allocate_slot();
			if (slot.is_null())
			{
				logger::error("CreateTexture '{}': CBV/SRV/UAV pool exhausted", desc.debugName);
				return TextureHandle{};
			}

			Texture texture(m_Device.Get(), m_CbvSrvUavHeap.Get(), slot.index, desc);

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = ConvertTextureSrvDesc(desc);
			m_Device->CreateShaderResourceView(
				texture.GetD3D12Resource(),
				&srvDesc,
				texture.GetCpuHandle());

			m_CbvSrvUavSlots[slot.index] = std::move(texture);
			return TextureHandle{ slot, desc.usage };
		}

		auto slot = m_Textures.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("CreateTexture '{}': texture pool exhausted", desc.debugName);
			return TextureHandle{};
		}
		m_Textures[slot.index] = Texture(m_Device.Get(), nullptr, slot.index, desc);
		return TextureHandle{ slot, desc.usage };
	}

	SamplerHandle
	ResourceManager::CreateSampler(const SamplerDesc& desc) noexcept
	{
		auto samplerSlotHandle = m_Samplers.try_allocate_slot();
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

		return SamplerHandle{ slotIndex, samplerSlotHandle.generation };
	}

	TextureHandle
	ResourceManager::CreateTexture(
		const TextureDesc&                      desc,
		std::span<const TextureSubresourceData> initialData) noexcept
	{
		TextureHandle handle = CreateTexture(desc);

		if (initialData.empty())
		{
			return handle;
		}

		// Own a contiguous copy of the decoded pixels so the deferred upload survives
		// until the next FlushPendingTextureUploads (the caller's data may be transient).
		PendingTextureUpload pending;
		pending.handle = handle;
		pending.subresources.reserve(initialData.size());

		for (const auto& sub : initialData)
		{
			const size_t sliceBytes = static_cast<size_t>(sub.slicePitch);
			const size_t offset     = pending.bytes.size();
			pending.bytes.resize(offset + sliceBytes);
			std::memcpy(pending.bytes.data() + offset, sub.data, sliceBytes);
			pending.subresources.push_back({ offset, sub.rowPitch, sub.slicePitch });
		}

		m_PendingTextureUploads.push_back(std::move(pending));

		return handle;
	}

	TextureHandle
	ResourceManager::CreateTexture(const assetlib::ImageData& image, std::string debugName) noexcept
	{
		// Map the decoded image's API-neutral TexFormat + layout onto an engine TextureDesc; the
		// Vulkan→DXGI→engine format translation stays here so callers never see graphics formats.
		TextureDesc desc;
		desc.width     = image.width;
		desc.height    = image.height;
		desc.mipLevels = image.mipLevels;
		desc.arraySize = image.arraySize;
		desc.format    = ConvertFormat(VkFormatToDXGI(image.vkFormat));
		desc.usage     = TextureUsageFlag::kSRV;
		desc.dimension =
			image.isCubemap ? TextureDimension::kTextureCube : TextureDimension::kTexture2D;
		desc.initalLayout = BarrierLayout::kCopyDest;
		desc.debugName    = std::move(debugName);

		std::vector<TextureSubresourceData> subresources;
		subresources.reserve(image.subresources.size());
		for (const auto& s : image.subresources)
		{
			subresources.push_back({ image.pixels.data() + s.offset, s.rowPitch, s.slicePitch });
		}

		return CreateTexture(desc, subresources);
	}

	TextureHandle
	ResourceManager::CreateSolidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept
	{
		const uint8_t pixel[4] = { r, g, b, a };

		TextureDesc desc;
		desc.width        = 1;
		desc.height       = 1;
		desc.format       = Format::RGBA8_UNORM;
		desc.usage        = TextureUsageFlag::kSRV;
		desc.initalLayout = BarrierLayout::kCopyDest;
		desc.debugName    = "Solid Texture";

		// Procedural, not a file load: build the 1x1 pixel and go through the same raw
		// upload path as any other texture.
		const TextureSubresourceData sub{ pixel, 4, 4 };
		return CreateTexture(desc, std::span<const TextureSubresourceData>(&sub, 1));
	}

	void
	ResourceManager::FlushPendingTextureUploads(ICommandList* cmd) noexcept
	{
		for (auto& pending : m_PendingTextureUploads)
		{
			// Rebuild the upload spans pointing into our owned byte buffer.
			std::vector<TextureSubresourceData> subresources;
			subresources.reserve(pending.subresources.size());
			for (const auto& s : pending.subresources)
			{
				subresources.push_back(
					{ pending.bytes.data() + s.offset, s.rowPitch, s.slicePitch });
			}

			cmd->WriteTexture(pending.handle, subresources);

			// COPY_DEST -> SHADER_RESOURCE so the forward pass can sample it. These
			// bindless textures aren't frame-graph resources, so we barrier directly.
			TextureBarrierDesc barrier;
			barrier.syncBefore   = BarrierSyncFlag::kCopy;
			barrier.accessBefore = BarrierAccessFlag::kCopyDest;
			barrier.layoutBefore = BarrierLayout::kCopyDest;
			barrier.syncAfter    = BarrierSyncFlag::kPixelShader;
			barrier.accessAfter  = BarrierAccessFlag::kShaderResource;
			barrier.layoutAfter  = BarrierLayout::kShaderResource;
			cmd->Barrier(pending.handle, barrier);
		}

		m_PendingTextureUploads.clear();
	}

	ReadbackBufferHandle
	ResourceManager::CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept
	{
		gassert(desc.byteSize > 0, "Readback buffer requires a positive byte size");

		auto slot = m_ReadbackBuffers.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("CreateReadbackBuffer: readback pool exhausted");
			return ReadbackBufferHandle{};
		}
		uint32_t slotIndex = slot.index;

		m_ReadbackBuffers[slotIndex] = ReadbackBuffer(m_Device.Get(), desc);

		return ReadbackBufferHandle{ slotIndex, slot.generation };
	}

	TextureHandle
	ResourceManager::CreateTexture(
		wrl::ComPtr<ID3D12Resource> d3d12Texture,
		const TextureDesc&          desc) noexcept
	{
		if (desc.usage.any(TextureUsageFlag::kSRV))
		{
			auto slot = m_CbvSrvUavSlots.try_allocate_slot();
			if (slot.is_null())
			{
				logger::error("CreateTexture '{}': CBV/SRV/UAV pool exhausted", desc.debugName);
				return TextureHandle{};
			}

			Texture texture(
				m_Device.Get(),
				m_CbvSrvUavHeap.Get(),
				slot.index,
				std::move(d3d12Texture),
				desc);

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = ConvertTextureSrvDesc(desc);
			m_Device->CreateShaderResourceView(
				texture.GetD3D12Resource(),
				&srvDesc,
				texture.GetCpuHandle());

			m_CbvSrvUavSlots[slot.index] = std::move(texture);
			return TextureHandle{ slot, desc.usage };
		}

		auto slot = m_Textures.try_allocate_slot();
		if (slot.is_null())
		{
			logger::error("CreateTexture '{}': texture pool exhausted", desc.debugName);
			return TextureHandle{};
		}
		m_Textures[slot.index] =
			Texture(m_Device.Get(), nullptr, slot.index, std::move(d3d12Texture), desc);
		return TextureHandle{ slot, desc.usage };
	}

	RtvHandle
	ResourceManager::CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept
	{
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
	ResourceManager::DestroyRtv(
		RtvHandle handle,
		uint64_t  currentFenceValue,
		bool      deferred) noexcept
	{
		gassert(ValidRtvHandle(handle), "Cannot destroy invalid RTV handle");

		if (deferred)
		{
			m_Rtvs.retire_slot(handle.idx);
			m_PendingDeletions.emplace_back(
				PendingDeletion::Type::kRtv,
				handle.idx,
				currentFenceValue);
		}
		else
		{
			m_Rtvs.release_slot(handle.idx);
		}
	}

	void
	ResourceManager::DestroyBuffer(
		BufferHandle handle,
		uint64_t     currentFenceValue,
		bool         deferred) noexcept
	{
		gassert(ValidBufferHandle(handle), "Cannot destroy invalid buffer handle");

		if (deferred)
		{
			m_CbvSrvUavSlots.retire_slot(handle.slot);
			m_PendingDeletions.emplace_back(
				PendingDeletion::Type::kCbvSrvUav,
				handle.slot.index,
				currentFenceValue);
		}
		else
		{
			m_CbvSrvUavSlots.release_slot(handle.slot);
		}
	}

	void
	ResourceManager::DestroyTexture(
		TextureHandle handle,
		uint64_t      currentFenceValue,
		bool          deferred) noexcept
	{
		gassert(ValidTextureHandle(handle), "Cannot destroy invalid texture handle");

		const bool isSrv = handle.usage.any(TextureUsageFlag::kSRV);

		if (deferred)
		{
			// SRV textures live in the CBV_SRV_UAV pool; RTV/DSV-only in m_Textures. Retiring now
			// stales every handle immediately; the resource and its descriptor index survive until
			// the sweep reclaims them.
			if (isSrv)
			{
				m_CbvSrvUavSlots.retire_slot(handle.slot);
			}
			else
			{
				m_Textures.retire_slot(handle.slot);
			}

			m_PendingDeletions.emplace_back(
				isSrv ? PendingDeletion::Type::kCbvSrvUav : PendingDeletion::Type::kTexture,
				handle.slot.index,
				currentFenceValue);
		}
		else if (isSrv)
		{
			m_CbvSrvUavSlots.release_slot(handle.slot);
		}
		else
		{
			m_Textures.release_slot(handle.slot);
		}
	}

	void
	ResourceManager::DestroySampler(
		SamplerHandle handle,
		uint64_t      currentFenceValue,
		bool          deferred) noexcept
	{
		gassert(ValidSamplerHandle(handle), "Cannot destroy invalid sampler handle");

		if (deferred)
		{
			m_Samplers.retire_slot(handle.idx);
			m_PendingDeletions.emplace_back(
				PendingDeletion::Type::kSampler,
				handle.idx,
				currentFenceValue);
		}
		else
		{
			m_Samplers.release_slot(handle.idx);
		}
	}

	void
	ResourceManager::DestroyReadbackBuffer(
		ReadbackBufferHandle handle,
		uint64_t             currentFenceValue,
		bool                 deferred) noexcept
	{
		gassert(ValidReadbackBufferHandle(handle), "Cannot destroy invalid readback buffer handle");

		if (deferred)
		{
			m_ReadbackBuffers.retire_slot(handle.idx);
			m_PendingDeletions.emplace_back(
				PendingDeletion::Type::kReadback,
				handle.idx,
				currentFenceValue);
		}
		else
		{
			m_ReadbackBuffers.release_slot(handle.idx);
		}
	}

	void
	ResourceManager::CleanupExpiredResources(uint64_t completedFenceValue) noexcept
	{
		// The slots were retired when the destroy was recorded, so their handles have been stale
		// since then. This half destroys the resource and returns the index to the free list.
		std::erase_if(m_PendingDeletions, [&](const auto& pending) {
			if (pending.fenceValue <= completedFenceValue)
			{
				switch (pending.type)
				{
				case PendingDeletion::Type::kCbvSrvUav:
					m_CbvSrvUavSlots.reclaim_slot(pending.slotIndex);
					break;
				case PendingDeletion::Type::kRtv:
					m_Rtvs.reclaim_slot(pending.slotIndex);
					break;
				case PendingDeletion::Type::kDsv:
					m_Dsvs.reclaim_slot(pending.slotIndex);
					break;
				case PendingDeletion::Type::kTexture:
					m_Textures.reclaim_slot(pending.slotIndex);
					break;
				case PendingDeletion::Type::kReadback:
					m_ReadbackBuffers.reclaim_slot(pending.slotIndex);
					break;
				case PendingDeletion::Type::kSampler:
					m_Samplers.reclaim_slot(pending.slotIndex);
					break;
				}
				return true;
			}
			return false;
		});
	}

	bool
	ResourceManager::ValidBufferHandle(const BufferHandle& handle) const noexcept
	{
		if (!m_CbvSrvUavSlots.valid(handle.slot))
		{
			return false;
		}

		const auto& slot = m_CbvSrvUavSlots[handle.slot];
		return std::holds_alternative<Buffer>(slot) && !std::get<Buffer>(slot).IsNull();
	}

	bool
	ResourceManager::ValidTextureHandle(const TextureHandle& handle) const noexcept
	{
		if (handle.usage.any(TextureUsageFlag::kSRV))
		{
			if (!m_CbvSrvUavSlots.valid(handle.slot))
			{
				return false;
			}

			const auto& slot = m_CbvSrvUavSlots[handle.slot];
			return std::holds_alternative<Texture>(slot) && !std::get<Texture>(slot).IsNull();
		}

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
		if (!m_ReadbackBuffers.valid(handle.idx, handle.generation))
		{
			return false;
		}

		return !m_ReadbackBuffers[handle.idx].IsNull();
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
		ID3D12DescriptorHeap* heaps[] = { m_CbvSrvUavHeap.Get(), m_SamplerHeap.Get() };
		cmdList->SetDescriptorHeaps(_countof(heaps), heaps);
	}

	const Texture&
	ResourceManager::GetTexture(TextureHandle handle) const noexcept
	{
		gassert(ValidTextureHandle(handle), "Invalid texture handle");
		if (handle.usage.any(TextureUsageFlag::kSRV))
		{
			return std::get<Texture>(m_CbvSrvUavSlots[handle.slot]);
		}
		return m_Textures[handle.slot];
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
		return std::get<Buffer>(m_CbvSrvUavSlots[handle.slot]);
	}

	const ReadbackBuffer&
	ResourceManager::GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept
	{
		gassert(ValidReadbackBufferHandle(handle), "Invalid readback buffer handle");
		return m_ReadbackBuffers[handle.idx];
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
		return m_ReadbackBuffers[handle.idx].Map();
	}

	void
	ResourceManager::UnmapReadback(ReadbackBufferHandle handle) noexcept
	{
		gassert(ValidReadbackBufferHandle(handle), "Invalid readback buffer handle");
		m_ReadbackBuffers[handle.idx].Unmap();
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
	ResourceManager::DestroyDsv(
		DsvHandle handle,
		uint64_t  currentFenceValue,
		bool      deferred) noexcept
	{
		gassert(ValidDsvHandle(handle), "Cannot destroy invalid RTV handle");

		if (deferred)
		{
			m_Dsvs.retire_slot(handle.idx);
			m_PendingDeletions.emplace_back(
				PendingDeletion::Type::kDsv,
				handle.idx,
				currentFenceValue);
		}
		else
		{
			m_Dsvs.release_slot(handle.idx);
		}
	}
}
