#include "cmd/CommandList_d3d12.h"
#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "cmd/Version.h"
#include "constants/constants.h"
#include "pipeline/MeshletPipeline_d3d12.h"
#include "resource/ResourceManager_d3d12.h"
#include "util_d3d12.h"
#include <core/math.h>

namespace bgl
{
	CommandList::CommandList(
		const CommandListDesc& desc,
		ICommandAllocator*     commandAllocator,
		ResourceManagerHandle  resourceManager) :
		m_Desc(desc), m_ResourceManager(std::move(resourceManager)),
		m_UploadManager(
			m_ResourceManager->As<ResourceManager>()->GetD3D12DeviceCpy(),
			desc.uploadChunkSize,
			0,
			false)
	{
		gassert(commandAllocator != nullptr, "Command allocator cannot be null");
		gassert(m_ResourceManager != nullptr, "Resource manager cannot be null");

		auto d3d12CommandAllocator =
			commandAllocator->As<CommandAllocator>()->GetD3D12CommandAllocator();

		gassert(d3d12CommandAllocator != nullptr, "D3D12 Command allocator cannot be null");

		wrl::ComPtr<ID3D12Device> device;
		d3d12CommandAllocator->GetDevice(IID_PPV_ARGS(&device)) >> d3d12ErrChecker;

		wrl::ComPtr<ID3D12Device4> device4;
		device->QueryInterface(IID_PPV_ARGS(&device4)) >> d3d12ErrChecker;

		auto d3d12CmdListType = ConvertQueueType(desc.type);

		wrl::ComPtr<ID3D12CommandList> commandList;
		device4->CreateCommandList1(
			0,
			d3d12CmdListType,
			D3D12_COMMAND_LIST_FLAG_NONE,
			IID_PPV_ARGS(&commandList)) >>
			d3d12ErrChecker;

		commandList->QueryInterface(IID_PPV_ARGS(&m_CommandList)) >> d3d12ErrChecker;
	}

	void
	CommandList::WriteBuffer(BufferHandle handle, const void* data, size_t offset, size_t byteSize)
	{
		auto& buffer = m_ResourceManager->GetBuffer(handle);
		auto& desc   = buffer.GetDesc();

		if (desc.cpuAccess == BufferDesc::CpuAccessMode::kUpload)
		{
			memcpy(static_cast<std::byte*>(buffer.GetMappedPtr()) + offset, data, byteSize);
			return;
		}
		else if (desc.cpuAccess == BufferDesc::CpuAccessMode::kReadBack)
		{
			gfatal("Cannot write to a readback buffer");
			return;
		}

		size_t                    offsetInUploadBuffer = 0;
		void*                     cpuVA;
		D3D12_GPU_VIRTUAL_ADDRESS gpuVA;

		auto success = m_UploadManager.SuballocateBuffer(
			m_LastCompletedFence,
			byteSize,
			nullptr,
			m_CurrentUploadBuffer,
			&offsetInUploadBuffer,
			&cpuVA,
			&gpuVA,
			m_RecordingVersion,
			D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

		gassert(success, "Failed to suballocate buffer");

		memcpy(cpuVA, data, byteSize);

		BufferBarrierDesc barrier = {};
		barrier.accessBefore      = BarrierAccessFlag::kNone;
		barrier.syncBefore        = BarrierSyncFlag::kNone;
		barrier.accessAfter       = BarrierAccessFlag::kCopyDest;
		barrier.syncAfter         = BarrierSyncFlag::kCopy;

		Barrier(handle, barrier);

		m_CommandList->CopyBufferRegion(
			buffer.GetD3D12Resource(),
			offset,
			m_CurrentUploadBuffer.Get(),
			offsetInUploadBuffer,
			byteSize);
	}

	void
	CommandList::Open(ICommandQueue* cmdQueue, ICommandAllocator* allocator)
	{
		gassert(!m_Open, "Command list is already open");
		gassert(cmdQueue != nullptr, "Command queue cannot be null");
		gassert(allocator != nullptr, "Command allocator cannot be null");

		auto* d3d12Allocator = allocator->As<CommandAllocator>()->GetD3D12CommandAllocator();

		gassert(d3d12Allocator != nullptr, "Command Allocator cannot be null");

		m_CommandList->Reset(d3d12Allocator, nullptr) >> d3d12ErrChecker;
		m_LastCompletedFence = cmdQueue->GetLastCompletedFence();
		m_RecordingVersion   = MakeVersion(cmdQueue->GetNextFenceValue(), m_Desc.type, false);

		auto                  d3d12ResourceManager = m_ResourceManager->As<ResourceManager>();
		ID3D12DescriptorHeap* heaps[]              = { d3d12ResourceManager->GetCbvSrvUavHeap() };
		m_CommandList->SetDescriptorHeaps(std::size(heaps), heaps);
		m_Open = true;
	}

	void
	CommandList::Close()
	{
		gassert(m_Open, "Command list must be open before closing");

		m_CommandList->Close() >> d3d12ErrChecker;
		m_CurrentMeshletState.reset();
		m_Open = false;
	}

	void
	CommandList::Barrier(BufferHandle handle, const BufferBarrierDesc& barrier)
	{
		auto&                                   buffer = m_ResourceManager->GetBuffer(handle);
		wrl::ComPtr<ID3D12GraphicsCommandList7> cmdList7;
		auto&                                   desc = buffer.GetDesc();

		m_CommandList->QueryInterface<ID3D12GraphicsCommandList7>(&cmdList7);

		D3D12_BUFFER_BARRIER bufferBarrier = {};

		bufferBarrier.SyncBefore   = ConvertBarrierSync(barrier.syncBefore);
		bufferBarrier.AccessBefore = ConvertBarrierAccess(barrier.accessBefore);

		bufferBarrier.SyncAfter   = ConvertBarrierSync(barrier.syncAfter);
		bufferBarrier.AccessAfter = ConvertBarrierAccess(barrier.accessAfter);

		bufferBarrier.pResource = buffer.GetD3D12Resource();
		bufferBarrier.Offset    = 0;
		bufferBarrier.Size      = static_cast<uint64_t>(desc.elementCount) * desc.stride;

		D3D12_BARRIER_GROUP group = {};
		group.Type                = D3D12_BARRIER_TYPE_BUFFER;
		group.NumBarriers         = 1;
		group.pBufferBarriers     = &bufferBarrier;

		cmdList7->Barrier(1, &group);
	}

	void
	CommandList::Barrier(TextureHandle handle, const TextureBarrierDesc& barrier)
	{
		auto& texture = m_ResourceManager->GetTexture(handle);

		D3D12_TEXTURE_BARRIER textureBarrier = {};

		textureBarrier.SyncBefore   = ConvertBarrierSync(barrier.syncBefore);
		textureBarrier.AccessBefore = ConvertBarrierAccess(barrier.accessBefore);

		textureBarrier.SyncAfter   = ConvertBarrierSync(barrier.syncAfter);
		textureBarrier.AccessAfter = ConvertBarrierAccess(barrier.accessAfter);

		textureBarrier.LayoutBefore = ConvertBarrierLayout(barrier.layoutBefore);
		textureBarrier.LayoutAfter  = ConvertBarrierLayout(barrier.layoutAfter);

		textureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

		textureBarrier.pResource = texture.GetD3D12Resource();

		if (barrier.mipCount == uint32_t(-1) && barrier.layerCount == uint32_t(-1))
		{
			textureBarrier.Subresources.IndexOrFirstMipLevel =
				D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		}
		else
		{
			textureBarrier.Subresources.IndexOrFirstMipLevel = barrier.baseMipLevel;
			textureBarrier.Subresources.NumMipLevels         = barrier.mipCount;
			textureBarrier.Subresources.FirstArraySlice      = barrier.baseArrayLayer;
			textureBarrier.Subresources.NumArraySlices       = barrier.layerCount;
			textureBarrier.Subresources.FirstPlane           = barrier.firstPlane;
			textureBarrier.Subresources.NumPlanes            = barrier.planeCount;
		}

		D3D12_BARRIER_GROUP group = {};
		group.Type                = D3D12_BARRIER_TYPE_TEXTURE;
		group.NumBarriers         = 1;
		group.pTextureBarriers    = &textureBarrier;

		m_CommandList->Barrier(1, &group);
	}

	void
	CommandList::Barrier(RtvHandle handle, const TextureBarrierDesc& barrier)
	{
		auto& rtv           = m_ResourceManager->GetRtv(handle);
		auto  textureHandle = rtv.GetTextureHandle();

		gassert(m_ResourceManager->ValidRtvHandle(handle), "RTV has invalid texture handle");
		Barrier(textureHandle, barrier);
	}

	void
	CommandList::SetMeshletState(const MeshletState& gfxState)
	{
		m_CurrentMeshletState = gfxState;
	}

	void
	CommandList::DispatchMesh(
		uint32_t threadGroupCountX,
		uint32_t threadGroupCountY,
		uint32_t threadGroupCountZ) const
	{
		gassert(m_CurrentMeshletState.has_value(), "Graphics state must be set before drawing");
		gassert(
			m_CurrentMeshletState->pipeline.IsInitialized(),
			"Pipeline state must be set in graphics state");

		// Viewport
		{
			const auto& viewports = m_CurrentMeshletState->viewportState.viewports;
			std::array<D3D12_VIEWPORT, ViewportState::MaxViewports> d3d12Viewports = {};

			for (size_t i = 0; i < viewports.size(); ++i)
			{
				const auto& vp   = viewports[i];
				auto&       view = d3d12Viewports[i];
				view.TopLeftX    = vp.minX;
				view.TopLeftY    = vp.minY;
				view.Width       = vp.maxX - vp.minX;
				view.Height      = vp.maxY - vp.minY;
				view.MinDepth    = vp.minZ;
				view.MaxDepth    = vp.maxZ;
			}

			m_CommandList->RSSetViewports(
				static_cast<UINT>(viewports.size()),
				d3d12Viewports.data());
		}

		// Scissor rect
		{
			const auto& scissorRects = m_CurrentMeshletState->viewportState.scissorRects;
			std::array<D3D12_RECT, ViewportState::MaxViewports> d3d12Rects = {};

			for (size_t i = 0; i < scissorRects.size(); ++i)
			{
				const auto& rc   = scissorRects[i];
				auto&       rect = d3d12Rects[i];
				rect.left        = rc.minX;
				rect.right       = rc.maxX;
				rect.top         = rc.minY;
				rect.bottom      = rc.maxY;
			}

			m_CommandList->RSSetScissorRects(
				static_cast<UINT>(scissorRects.size()),
				d3d12Rects.data());
		}

		// Render targets
		{
			const auto& rtvs = m_CurrentMeshletState->frameBuffer.colorAttachments;
			std::array<D3D12_CPU_DESCRIPTOR_HANDLE, c_MaxRenderTargets> d3d12RenderTargets = {};

			for (size_t i = 0; i < rtvs.size(); ++i)
			{
				auto& rtv             = m_ResourceManager->GetRtv(rtvs[i]);
				d3d12RenderTargets[i] = rtv.GetCpuHandle();
			}

			m_CommandList->OMSetRenderTargets(
				static_cast<UINT>(rtvs.size()),
				d3d12RenderTargets.data(),
				FALSE,
				nullptr);
		}

		// Pipeline state
		{
			auto* pipeline = m_CurrentMeshletState->pipeline->As<MeshletPipeline>();

			m_CommandList->SetPipelineState(pipeline->GetPipelineState());
			m_CommandList->SetGraphicsRootSignature(pipeline->GetRootSignature());

			auto rootConstantsData = m_CurrentMeshletState->uniforms->Data();

			if (pipeline->GetUniformSize() != 0)
			{
				gassert(rootConstantsData != nullptr, "Pipeline expects uniforms but none are set");
				auto num32BitValues = core::align(pipeline->GetUniformSize(), 4) / 4;
				m_CommandList
					->SetGraphicsRoot32BitConstants(0, num32BitValues, rootConstantsData, 0);
			}
			else
			{
				gassert(
					rootConstantsData == nullptr,
					"Root constants data set but pipeline has no expected uniforms");
			}
		}

		wrl::ComPtr<ID3D12GraphicsCommandList6> cmdList6;
		if (SUCCEEDED(m_CommandList->QueryInterface(IID_PPV_ARGS(&cmdList6))))
		{
			cmdList6->DispatchMesh(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
		}
		else
		{
			gassert(
				false,
				"Device/Driver does not support Mesh Shading (DirectX 12 Agility SDK / Feature "
				"Level 12_2 required)");
		}
	}

	void
	CommandList::SubmitChunks(ICommandQueue* cmdQueue)
	{
		gassert(cmdQueue != nullptr, "Command queue cannot be null");

		auto submittedVersion = MakeVersion(cmdQueue->GetNextFenceValue(), m_Desc.type, true);
		m_UploadManager.SubmitChunks(m_RecordingVersion, submittedVersion);
		m_RecordingVersion = 0;
	}

}
