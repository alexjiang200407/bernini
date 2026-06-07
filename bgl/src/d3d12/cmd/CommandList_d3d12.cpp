#include "cmd/CommandList_d3d12.h"
#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandList.h"
#include "constants/constants.h"
#include "pipeline/GraphicsPipeline_d3d12.h"
#include "resource/ResourceManager_d3d12.h"
#include "util.h"

namespace bgl
{
	CommandList::CommandList(
		QueueType             type,
		ICommandAllocator*    commandAllocator,
		ResourceManagerHandle resourceManager) :
		m_Type(type), m_ResourceManager(std::move(resourceManager))
	{
		auto d3d12CommandAllocator =
			commandAllocator->As<CommandAllocator>()->GetD3D12CommandAllocator();
		gassert(d3d12CommandAllocator != nullptr, "D3D12 Command allocator cannot be null");

		wrl::ComPtr<ID3D12Device> device;
		d3d12CommandAllocator->GetDevice(IID_PPV_ARGS(&device)) >> d3d12ErrChecker;

		wrl::ComPtr<ID3D12Device4> device4;
		device->QueryInterface(IID_PPV_ARGS(&device4)) >> d3d12ErrChecker;

		auto d3d12CmdListType = ConvertQueueType(type);

		wrl::ComPtr<ID3D12CommandList> commandList;
		device4->CreateCommandList1(
			0,
			d3d12CmdListType,
			D3D12_COMMAND_LIST_FLAG_NONE,
			IID_PPV_ARGS(&commandList)) >>
			d3d12ErrChecker;

		commandList->QueryInterface(IID_PPV_ARGS(&m_CommandList)) >> d3d12ErrChecker;
	}

	CommandList::~CommandList() noexcept { logger::info("Destroying CommandList"); }

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

		wrl::ComPtr<ID3D12Device10> device10;
		m_CommandList->GetDevice(IID_PPV_ARGS(&device10)) >> d3d12ErrChecker;

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type                  = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC1 resDesc = {};
		resDesc.Dimension            = D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Width                = byteSize;
		resDesc.Height               = 1;
		resDesc.DepthOrArraySize     = 1;
		resDesc.MipLevels            = 1;
		resDesc.Format               = DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count     = 1;
		resDesc.Layout               = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		device10->CreateCommittedResource3(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&resDesc,
			D3D12_BARRIER_LAYOUT_UNDEFINED,  // Correct layout state rule for buffers
			nullptr,
			nullptr,
			0,
			nullptr,
			IID_PPV_ARGS(&m_StagingBuffer)) >>
			d3d12ErrChecker;

		void*       mappedPtr = nullptr;
		D3D12_RANGE readRange = { 0, 0 };
		m_StagingBuffer->Map(0, &readRange, &mappedPtr);

		memcpy(static_cast<std::byte*>(mappedPtr) + offset, data, byteSize);

		m_StagingBuffer->Unmap(0, nullptr);

		BufferBarrierDesc barrier = {};
		barrier.accessBefore      = BarrierAccessFlag::kNone;
		barrier.syncBefore        = BarrierSyncFlag::kNone;
		barrier.accessAfter       = BarrierAccessFlag::kCopyDest;
		barrier.syncAfter         = BarrierSyncFlag::kCopy;

		Barrier(handle, barrier);

		m_CommandList->CopyBufferRegion(
			buffer.GetD3D12Resource(),
			offset,
			m_StagingBuffer.Get(),
			0,
			byteSize);
	}

	void
	CommandList::Open(ICommandAllocator* allocator)
	{
		gassert(!m_Open, "Command list is already open");

		auto* d3d12Allocator = allocator->As<CommandAllocator>()->GetD3D12CommandAllocator();
		gassert(d3d12Allocator != nullptr, "Command Allocator cannot be null");

		m_CommandList->Reset(d3d12Allocator, nullptr) >> d3d12ErrChecker;

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
		m_CurrentGraphicsState.reset();
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
		bufferBarrier.Size      = desc.byteSize;

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
		auto& desc    = texture.GetDesc();

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
	CommandList::SetGraphicsState(const GraphicsState& gfxState)
	{
		m_CurrentGraphicsState = gfxState;
	}

	void
	CommandList::DrawInstanced(uint32_t vertexCount, uint32_t instanceCount) const
	{
		gassert(m_CurrentGraphicsState.has_value(), "Graphics state must be set before drawing");
		gassert(
			m_CurrentGraphicsState->pipeline.IsInitialized(),
			"Pipeline state must be set in graphics state");

		// Viewport
		{
			const auto& viewports = m_CurrentGraphicsState->viewportState.viewports;
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
			const auto& scissorRects = m_CurrentGraphicsState->viewportState.scissorRects;
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
			const auto& rtvs = m_CurrentGraphicsState->frameBuffer.colorAttachments;
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
			auto* pipeline = m_CurrentGraphicsState->pipeline->As<GraphicsPipeline>();
			m_CommandList->IASetPrimitiveTopology(pipeline->GetPrimitiveTopology());
			m_CommandList->SetPipelineState(pipeline->GetPipelineState());
			m_CommandList->SetGraphicsRootSignature(pipeline->GetRootSignature());

			if (auto rootConstantsData = m_CurrentGraphicsState->rootConstantData;
			    rootConstantsData != nullptr && m_CurrentGraphicsState->rootConstantSize != 0)
			{
				auto num32BitValues = (m_CurrentGraphicsState->rootConstantSize + 3) / 4;
				m_CommandList
					->SetGraphicsRoot32BitConstants(0, num32BitValues, rootConstantsData, 0);
			}
		}

		m_CommandList->DrawInstanced(vertexCount, instanceCount, 0, 0);
	}

}
