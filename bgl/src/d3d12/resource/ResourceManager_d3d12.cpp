#pragma once
#include "d3d12/resource/ResourceManager_d3d12.h"
#include "util.h"

namespace bgl
{
	BufferHandle
	ResourceManager::CreateRawBuffer(const BufferDesc& desc)
	{
		gassert(IsInitialized(), "ResourceManager must be initialized");
		return GetImpl()->CreateRawBuffer(desc);
	}

	TextureHandle
	ResourceManager::CreateTexture(const TextureDesc& desc)
	{
		gassert(IsInitialized(), "ResourceManager must be initialized");
		return GetImpl()->CreateTexture(desc);
	}

	void
	ResourceManager::DestroyBuffer(BufferHandle handle, uint64_t currentFenceValue)
	{
		gassert(IsInitialized(), "ResourceManager must be initialized");
		GetImpl()->DestroyBuffer(handle, currentFenceValue);
	}

	void
	ResourceManager::DestroyTexture(TextureHandle handle, uint64_t currentFenceValue)
	{
		gassert(IsInitialized(), "ResourceManager must be initialized");
		GetImpl()->DestroyTexture(handle, currentFenceValue);
	}

	void
	ResourceManager::CleanupExpiredResources(uint64_t completedFenceValue)
	{
		gassert(IsInitialized(), "ResourceManager must be initialized");
		GetImpl()->CleanupExpiredResources(completedFenceValue);
	}

	RtvHandle
	ResourceManager::CreateRtv(TextureHandle textureHandle, const RtvDesc& desc)
	{
		gassert(IsInitialized(), "ResourceManager must be initialized");
		return GetImpl()->CreateRtv(textureHandle, desc);
	}

	ResourceManagerImpl::ResourceManagerImpl(
		wrl::ComPtr<ID3D12Device> device,
		uint32_t                  maxDescriptors,
		uint32_t                  maxRtvs) :
		m_Device(std::move(device)), m_CbvSrvUavSlots(maxDescriptors), m_Rtvs(maxRtvs)
	{
		gassert(maxDescriptors > 0, "maxDescriptors must be greater than zero");

		// Get descriptor size for this device
		m_CbvSrvUavDescriptorSize =
			m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_RtvDescriptorSize =
			m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Create descriptor heap
		{
			D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
			heapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapDesc.NumDescriptors             = maxDescriptors;
			heapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

			m_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_CbvSrvUavHeap)) >>
				d3d12ErrChecker;
		}

		{
			D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
			rtvHeapDesc.NumDescriptors             = maxRtvs;
			rtvHeapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtvHeapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RtvHeap)) >>
				d3d12ErrChecker;
		}
	}

	ResourceManagerImpl::~ResourceManagerImpl() noexcept {}

	BufferHandle
	ResourceManagerImpl::CreateRawBuffer(const BufferDesc& desc)
	{
		auto bufferSlotHandle = m_CbvSrvUavSlots.allocate_slot();

		uint32_t slotIndex = bufferSlotHandle.index;

		Buffer buffer(m_Device.Get(), m_CbvSrvUavHeap.Get(), slotIndex, desc);

		if (desc.isUav)
		{
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_BUFFER;
			uavDesc.Format              = DXGI_FORMAT_R32_TYPELESS;  // Match raw buffer
			uavDesc.Buffer.FirstElement = 0;
			uavDesc.Buffer.NumElements  = static_cast<uint32_t>(desc.byteSize / sizeof(uint32_t));
			uavDesc.Buffer.StructureByteStride = 0;
			uavDesc.Buffer.Flags               = D3D12_BUFFER_UAV_FLAG_RAW;

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

			srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;

			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements  = static_cast<uint32_t>(desc.byteSize / sizeof(uint32_t));
			srvDesc.Buffer.StructureByteStride = 0;

			srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;

			m_Device->CreateShaderResourceView(
				buffer.GetD3D12Resource(),
				&srvDesc,
				buffer.GetCpuHandle());
		}

		m_CbvSrvUavSlots[slotIndex] = std::move(buffer);

		return BufferHandle{ slotIndex, bufferSlotHandle.generation };
	}

	TextureHandle
	ResourceManagerImpl::CreateTexture(const TextureDesc& desc)
	{
		gfatal("ResourceManagerImpl::CreateTexture not implemented yet");
		return TextureHandle();
	}

	TextureHandle
	ResourceManagerImpl::CreateTexture(
		wrl::ComPtr<ID3D12Resource> d3d12Texture,
		const TextureDesc&          desc)
	{
		auto     textureSlotHandle = m_CbvSrvUavSlots.allocate_slot();
		uint32_t slotIndex         = textureSlotHandle.index;

		Texture texture(m_Device.Get(), m_CbvSrvUavHeap.Get(), slotIndex, d3d12Texture, desc);

		m_CbvSrvUavSlots[slotIndex] = std::move(texture);

		return TextureHandle{ slotIndex, textureSlotHandle.generation };
	}

	RtvHandle
	ResourceManagerImpl::CreateRtv(TextureHandle textureHandle, const RtvDesc& desc)
	{
		auto&                       texture       = GetTexture(textureHandle);
		wrl::ComPtr<ID3D12Resource> resource      = texture.GetD3D12ResourceCopy();
		auto                        rtvSlotHandle = m_Rtvs.allocate_slot();

		uint32_t descriptorIndex = rtvSlotHandle.index;
		Rtv      rtv(m_Device.Get(), textureHandle, m_RtvHeap.Get(), descriptorIndex, desc);

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format        = ConvertFormat(desc.format);
		rtvDesc.ViewDimension = ConvertDimension(desc.dimension);

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
	ResourceManagerImpl::DestroyRtv(RtvHandle handle, uint64_t currentFenceValue)
	{
		gassert(ValidRtvHandle(handle), "Cannot destroy invalid RTV handle");
		m_PendingDeletions.push_back(
			{ PendingDeletion::Type::kRtv, handle.idx, currentFenceValue });
	}

	void
	ResourceManagerImpl::DestroyBuffer(BufferHandle handle, uint64_t currentFenceValue)
	{
		gassert(ValidBufferHandle(handle), "Cannot destroy invalid buffer handle");
		m_PendingDeletions.push_back(
			{ PendingDeletion::Type::kCbvSrvUav, handle.idx, currentFenceValue });
	}

	void
	ResourceManagerImpl::DestroyTexture(TextureHandle handle, uint64_t currentFenceValue)
	{
		gassert(ValidTextureHandle(handle), "Cannot destroy invalid texture handle");
		m_PendingDeletions.push_back(
			{ PendingDeletion::Type::kCbvSrvUav, handle.idx, currentFenceValue });
	}

	void
	ResourceManagerImpl::CleanupExpiredResources(uint64_t completedFenceValue)
	{
		for (int i = static_cast<int>(m_PendingDeletions.size()) - 1; i >= 0; --i)
		{
			const auto& pending = m_PendingDeletions[i];

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
				}

				m_PendingDeletions[i] = m_PendingDeletions.back();
				m_PendingDeletions.pop_back();
			}
		}
	}

	bool
	ResourceManagerImpl::ValidBufferHandle(const BufferHandle& handle) const
	{
		if (!m_CbvSrvUavSlots.valid(handle.idx, handle.generation))
		{
			return false;
		}

		const auto& slot = m_CbvSrvUavSlots[handle.idx];
		return std::holds_alternative<Buffer>(slot) && !std::get<Buffer>(slot).IsNull();
	}

	bool
	ResourceManagerImpl::ValidTextureHandle(const TextureHandle& handle) const
	{
		if (!m_CbvSrvUavSlots.valid(handle.idx, handle.generation))
		{
			return false;
		}

		const auto& slot = m_CbvSrvUavSlots[handle.idx];
		return std::holds_alternative<Texture>(slot) && !std::get<Texture>(slot).IsNull();
	}

	bool
	ResourceManagerImpl::ValidRtvHandle(const RtvHandle& handle) const
	{
		if (!m_Rtvs.valid(handle.idx, handle.generation))
		{
			return false;
		}

		return !m_Rtvs[handle.idx].IsNull();
	}

	void
	ResourceManagerImpl::SetDescriptorHeap(ID3D12CommandList* cmdList)
	{
		gassert(cmdList != nullptr, "Command list cannot be null");
		ID3D12DescriptorHeap* heaps[] = { m_CbvSrvUavHeap.Get() };
		m_CommandList->SetDescriptorHeaps(_countof(heaps), heaps);
	}

	const Texture&
	ResourceManagerImpl::GetTexture(TextureHandle handle) const
	{
		gassert(ValidTextureHandle(handle), "Invalid texture handle");
		return std::get<Texture>(m_CbvSrvUavSlots[handle.idx]);
	}

	const Buffer&
	ResourceManagerImpl::GetBuffer(BufferHandle handle) const
	{
		gassert(ValidBufferHandle(handle), "Invalid buffer handle");
		return std::get<Buffer>(m_CbvSrvUavSlots[handle.idx]);
	}

	const Rtv&
	ResourceManagerImpl::GetRtv(RtvHandle handle) const
	{
		gassert(ValidRtvHandle(handle), "Invalid RTV handle");
		return m_Rtvs[handle.idx];
	}

	const Rtv&
	ResourceManager::GetRtv(const RtvHandle& handle) const
	{
		return GetImpl()->GetRtv(handle);
	}

	const Buffer&
	ResourceManager::GetBuffer(const BufferHandle& handle) const
	{
		return GetImpl()->GetBuffer(handle);
	}

	const Texture&
	ResourceManager::GetTexture(const TextureHandle& handle) const
	{
		return GetImpl()->GetTexture(handle);
	}
}
