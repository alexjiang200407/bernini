#pragma once
#include "d3d12/resource/ResourceManager_d3d12.h"
#include "cmd/CommandList.h"
#include "cmd/CommandList_d3d12.h"
#include "util_d3d12.h"

namespace bgl
{
	ResourceManager::ResourceManager(
		wrl::ComPtr<ID3D12Device>  device,
		const ResourceManagerDesc& desc) :
		m_Desc(desc), m_Device(std::move(device)), m_CbvSrvUavSlots(desc.maxCbvSrvUavs),
		m_Textures(desc.maxTextures), m_Rtvs(desc.maxRtvs), m_Dsvs(desc.maxDsvs)
	{
		gassert(desc.maxCbvSrvUavs > 0, "maxDescriptors must be greater than zero");
		gassert(desc.maxDsvs > 0, "maxDsvs must be greater than zero");
		gassert(desc.maxRtvs > 0, "maxRtvs must be greater than zero");
		gassert(desc.maxTextures > 0, "maxTextures must be greater than zero");

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
	}

	BufferHandle
	ResourceManager::CreateStructBuffer(const StructBufferDesc& desc)
	{
		gassert(desc.stride > 0, "StructuredBuffer requires a valid structural stride");
		gassert(desc.elementCount > 0, "StructuredBuffer requires a valid element count");

		auto     bufferSlotHandle = m_CbvSrvUavSlots.allocate_slot();
		uint32_t slotIndex        = bufferSlotHandle.index;

		Buffer buffer(m_Device.Get(), m_CbvSrvUavHeap.Get(), slotIndex, desc);

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

		m_CbvSrvUavSlots[slotIndex] = std::move(buffer);

		return BufferHandle{ slotIndex, bufferSlotHandle.generation };
	}

	TextureHandle
	ResourceManager::CreateTexture(const TextureDesc& desc)
	{
		auto     textureSlotHandle = m_Textures.allocate_slot();
		uint32_t slotIndex         = textureSlotHandle.index;

		Texture texture(m_Device.Get(), slotIndex, desc);

		m_Textures[slotIndex] = std::move(texture);

		return TextureHandle(slotIndex, textureSlotHandle.generation);
	}

	TextureHandle
	ResourceManager::CreateTexture(
		wrl::ComPtr<ID3D12Resource> d3d12Texture,
		const TextureDesc&          desc)
	{
		auto     textureSlotHandle = m_Textures.allocate_slot();
		uint32_t slotIndex         = textureSlotHandle.index;

		Texture texture(m_Device.Get(), slotIndex, std::move(d3d12Texture), desc);

		m_Textures[slotIndex] = std::move(texture);

		return TextureHandle{ slotIndex, textureSlotHandle.generation };
	}

	RtvHandle
	ResourceManager::CreateRtv(TextureHandle textureHandle, const RtvDesc& desc)
	{
		auto&                       texture       = GetTexture(textureHandle);
		wrl::ComPtr<ID3D12Resource> resource      = texture.GetD3D12ResourceCopy();
		auto                        rtvSlotHandle = m_Rtvs.allocate_slot();

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
	ResourceManager::DestroyRtv(RtvHandle handle, uint64_t currentFenceValue, bool deferred)
	{
		gassert(ValidRtvHandle(handle), "Cannot destroy invalid RTV handle");

		if (deferred)
		{
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
	ResourceManager::DestroyBuffer(BufferHandle handle, uint64_t currentFenceValue, bool deferred)
	{
		gassert(ValidBufferHandle(handle), "Cannot destroy invalid buffer handle");

		if (deferred)
		{
			m_PendingDeletions.emplace_back(
				PendingDeletion::Type::kCbvSrvUav,
				handle.idx,
				currentFenceValue);
		}
		else
		{
			m_CbvSrvUavSlots.release_slot(handle.idx);
		}
	}

	void
	ResourceManager::DestroyTexture(TextureHandle handle, uint64_t currentFenceValue, bool deferred)
	{
		gassert(ValidTextureHandle(handle), "Cannot destroy invalid texture handle");

		if (deferred)
		{
			m_PendingDeletions.emplace_back(
				PendingDeletion::Type::kTexture,
				handle.idx,
				currentFenceValue);
		}
		else
		{
			m_Textures.release_slot(handle.idx);
		}
	}

	void
	ResourceManager::CleanupExpiredResources(uint64_t completedFenceValue)
	{
		std::erase_if(m_PendingDeletions, [&](const auto& pending) {
			if (pending.fenceValue <= completedFenceValue)
			{
				switch (pending.type)
				{
				case PendingDeletion::Type::kCbvSrvUav:
					m_CbvSrvUavSlots.release_slot(pending.slotIndex);
					break;
				case PendingDeletion::Type::kRtv:
					m_Rtvs.release_slot(pending.slotIndex);
					break;
				case PendingDeletion::Type::kDsv:
					m_Dsvs.release_slot(pending.slotIndex);
					break;
				case PendingDeletion::Type::kTexture:
					m_Textures.release_slot(pending.slotIndex);
					break;
				}
				return true;
			}
			return false;
		});
	}

	bool
	ResourceManager::ValidBufferHandle(const BufferHandle& handle) const
	{
		if (!m_CbvSrvUavSlots.valid(handle.idx, handle.generation))
		{
			return false;
		}

		const auto& slot = m_CbvSrvUavSlots[handle.idx];
		return std::holds_alternative<Buffer>(slot) && !std::get<Buffer>(slot).IsNull();
	}

	bool
	ResourceManager::ValidTextureHandle(const TextureHandle& handle) const
	{
		if (!m_Textures.valid(handle.idx, handle.generation))
		{
			return false;
		}

		return !m_Textures[handle.idx].IsNull();
	}

	bool
	ResourceManager::ValidRtvHandle(const RtvHandle& handle) const
	{
		if (!m_Rtvs.valid(handle.idx, handle.generation))
		{
			return false;
		}

		return !m_Rtvs[handle.idx].IsNull();
	}

	void
	ResourceManager::SetDescriptorHeap(ID3D12GraphicsCommandList* cmdList)
	{
		gassert(cmdList != nullptr, "Command list cannot be null");
		ID3D12DescriptorHeap* heaps[] = { m_CbvSrvUavHeap.Get() };
		cmdList->SetDescriptorHeaps(_countof(heaps), heaps);
	}

	const Texture&
	ResourceManager::GetTexture(TextureHandle handle) const
	{
		gassert(ValidTextureHandle(handle), "Invalid texture handle");
		return m_Textures[handle.idx];
	}

	const Buffer&
	ResourceManager::GetBuffer(BufferHandle handle) const
	{
		gassert(ValidBufferHandle(handle), "Invalid buffer handle");
		return std::get<Buffer>(m_CbvSrvUavSlots[handle.idx]);
	}

	const Rtv&
	ResourceManager::GetRtv(RtvHandle handle) const
	{
		gassert(ValidRtvHandle(handle), "Invalid RTV handle");
		return m_Rtvs[handle.idx];
	}

	void
	ResourceManager::ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4])
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
	ResourceManager::CreateDsv(TextureHandle textureHandle, const DsvDesc& desc)
	{
		auto&                       texture       = GetTexture(textureHandle);
		wrl::ComPtr<ID3D12Resource> resource      = texture.GetD3D12ResourceCopy();
		auto                        dsvSlotHandle = m_Dsvs.allocate_slot();

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
	ResourceManager::GetDsv(DsvHandle handle) const
	{
		gassert(ValidDsvHandle(handle), "Invalid RTV handle");
		return m_Dsvs[handle.idx];
	}

	bool
	ResourceManager::ValidDsvHandle(const DsvHandle& handle) const
	{
		if (!m_Dsvs.valid(handle.idx, handle.generation))
		{
			return false;
		}

		return !m_Dsvs[handle.idx].IsNull();
	}

	void
	ResourceManager::ClearDsv(ICommandList* cmdList, DsvHandle handle, float depth, uint8_t stencil)
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
	ResourceManager::DestroyDsv(DsvHandle handle, uint64_t currentFenceValue, bool deferred)
	{
		gassert(ValidDsvHandle(handle), "Cannot destroy invalid RTV handle");

		if (deferred)
		{
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
