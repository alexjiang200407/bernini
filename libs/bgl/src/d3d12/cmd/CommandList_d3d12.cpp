#include "cmd/CommandList_d3d12.h"
#include "cmd/CommandAllocator_d3d12.h"
#include "cmd/CommandList.h"
#include "cmd/CommandQueue.h"
#include "cmd/Version.h"
#include "constants/constants.h"
#include "convert_d3d12.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline_d3d12.h"
#include "pipeline/MeshletKernel.h"
#include "pipeline/MeshletPipeline_d3d12.h"
#include "resource/ResourceManager_d3d12.h"
#include "uniforms/Uniforms.h"
#include <core/math.h>

#if defined(USE_PIX) && defined(_WIN32)
#	include <pix3.h>
#endif

namespace bgl
{
	CommandList::CommandList(
		const CommandListDesc& desc,
		ICommandAllocator*     commandAllocator,
		ResourceManagerRef     resourceManager) :
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

		D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
		argDesc.Type                         = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;

		D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
		sigDesc.ByteStride                   = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
		sigDesc.NumArgumentDescs             = 1;
		sigDesc.pArgumentDescs               = &argDesc;

		device->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&m_MeshDispatchSig)) >>
			d3d12ErrChecker;
	}

	void
	CommandList::WriteBuffer(
		BufferHandle handle,
		const void*  data,
		size_t       gpuBufferOffset,
		size_t       byteSize) noexcept
	{
		auto& buffer = m_ResourceManager->GetBuffer(handle);

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

		m_CommandList->CopyBufferRegion(
			buffer.GetD3D12Resource(),
			gpuBufferOffset,
			m_CurrentUploadBuffer.Get(),
			offsetInUploadBuffer,
			byteSize);
	}

	void
	CommandList::WriteTexture(
		TextureHandle                           handle,
		std::span<const TextureSubresourceData> subresources) noexcept
	{
		auto& texture = m_ResourceManager->GetTexture(handle);
		auto  device  = m_ResourceManager->As<ResourceManager>()->GetD3D12DeviceCpy();
		auto  texDesc = texture.GetD3D12Resource()->GetDesc();

		const uint32_t numSubresources = static_cast<uint32_t>(subresources.size());

		// Padded, copyable layout of each subresource within a linear upload buffer.
		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(numSubresources);
		std::vector<UINT>                               numRows(numSubresources);
		std::vector<UINT64>                             rowSizes(numSubresources);
		UINT64                                          totalBytes = 0;
		device->GetCopyableFootprints(
			&texDesc,
			0,
			numSubresources,
			0,
			footprints.data(),
			numRows.data(),
			rowSizes.data(),
			&totalBytes);

		size_t                    offsetInUploadBuffer = 0;
		void*                     cpuVA                = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS gpuVA                = 0;

		auto success = m_UploadManager.SuballocateBuffer(
			m_LastCompletedFence,
			totalBytes,
			nullptr,
			m_CurrentUploadBuffer,
			&offsetInUploadBuffer,
			&cpuVA,
			&gpuVA,
			m_RecordingVersion,
			D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

		gassert(success, "Failed to suballocate texture upload");

		// Copy each subresource into its padded footprint (RowPitch >= source pitch).
		for (uint32_t i = 0; i < numSubresources; ++i)
		{
			const auto& fp  = footprints[i];
			auto*       dst = static_cast<uint8_t*>(cpuVA) + fp.Offset;
			const auto& src = subresources[i];

			for (UINT row = 0; row < numRows[i]; ++row)
			{
				std::memcpy(
					dst + static_cast<uint64_t>(row) * fp.Footprint.RowPitch,
					static_cast<const uint8_t*>(src.data) +
						static_cast<uint64_t>(row) * src.rowPitch,
					rowSizes[i]);
			}
		}

		for (uint32_t i = 0; i < numSubresources; ++i)
		{
			// Footprint offsets are relative to the suballocated region; shift them to
			// the chunk resource that CopyTextureRegion sources from.
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed = footprints[i];
			placed.Offset += offsetInUploadBuffer;

			D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
			dstLocation.pResource                   = texture.GetD3D12Resource();
			dstLocation.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dstLocation.SubresourceIndex            = i;

			D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
			srcLocation.pResource                   = m_CurrentUploadBuffer.Get();
			srcLocation.Type                        = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			srcLocation.PlacedFootprint             = placed;

			m_CommandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
		}
	}

	void
	CommandList::CopyBufferToReadback(ReadbackBufferHandle dst, BufferHandle src) noexcept
	{
		auto&       srcBuffer = m_ResourceManager->GetBuffer(src);
		const auto& readback  = m_ResourceManager->GetReadbackBuffer(dst);
		const auto& desc      = srcBuffer.GetDesc();

		const uint64_t byteSize = desc.byteSize;

		gassert(
			readback.GetByteSize() >= byteSize,
			"Readback buffer is too small for the source buffer");

		m_CommandList->CopyBufferRegion(
			readback.GetD3D12Resource(),
			0,
			srcBuffer.GetD3D12Resource(),
			0,
			byteSize);
	}

	void
	CommandList::CopyTextureToReadback(ReadbackBufferHandle dst, TextureHandle src) noexcept
	{
		auto&       srcTexture = m_ResourceManager->GetTexture(src);
		const auto& readback   = m_ResourceManager->GetReadbackBuffer(dst);

		auto device  = m_ResourceManager->As<ResourceManager>()->GetD3D12DeviceCpy();
		auto texDesc = srcTexture.GetD3D12Resource()->GetDesc();

		// The readback buffer holds the texture in a linear, row-padded layout.
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
		device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, nullptr);

		D3D12_TEXTURE_COPY_LOCATION dstLocation = {};
		dstLocation.pResource                   = readback.GetD3D12Resource();
		dstLocation.Type                        = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		dstLocation.PlacedFootprint             = footprint;

		D3D12_TEXTURE_COPY_LOCATION srcLocation = {};
		srcLocation.pResource                   = srcTexture.GetD3D12Resource();
		srcLocation.Type                        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		srcLocation.SubresourceIndex            = 0;

		m_CommandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);
	}

	void
	CommandList::Open(ICommandQueue* cmdQueue, ICommandAllocator* allocator) noexcept
	{
		gassert(!m_Open, "Command list is already open");
		gassert(cmdQueue != nullptr, "Command queue cannot be null");
		gassert(allocator != nullptr, "Command allocator cannot be null");
		gassert(
			m_BoundQueue == nullptr || m_BoundQueue == cmdQueue,
			"A command list must always be opened with the queue that first opened it");
		m_BoundQueue = cmdQueue;

		auto* d3d12Allocator = allocator->As<CommandAllocator>()->GetD3D12CommandAllocator();

		gassert(d3d12Allocator != nullptr, "Command Allocator cannot be null");

		m_CommandList->Reset(d3d12Allocator, nullptr) >> d3d12ErrChecker;
		m_LastCompletedFence = cmdQueue->GetLastCompletedFence();
		m_RecordingVersion   = MakeVersion(cmdQueue->GetNextFenceValue(), m_Desc.type, false);

		auto                  d3d12ResourceManager = m_ResourceManager->As<ResourceManager>();
		ID3D12DescriptorHeap* heaps[]              = {
			d3d12ResourceManager->GetCbvSrvUavHeap(),
			d3d12ResourceManager->GetSamplerHeap(),
		};
		m_CommandList->SetDescriptorHeaps(std::size(heaps), heaps);
		m_Open = true;
	}

	void
	CommandList::Close() noexcept
	{
		gassert(m_Open, "Command list must be open before closing");

		m_CommandList->Close() >> d3d12ErrChecker;
		m_CurrentMeshletState.reset();
		m_Open = false;
	}

	void
	CommandList::BeginEvent([[maybe_unused]] std::string_view name) noexcept
	{
#if defined(USE_PIX) && defined(_WIN32)
		PIXBeginEvent(
			m_CommandList.Get(),
			PIX_COLOR_DEFAULT,
			"%.*s",
			static_cast<int>(name.size()),
			name.data());
#endif
	}

	void
	CommandList::EndEvent() noexcept
	{
#if defined(USE_PIX) && defined(_WIN32)
		PIXEndEvent(m_CommandList.Get());
#endif
	}

	namespace
	{
		D3D12_BUFFER_BARRIER
		MakeBufferBarrier(const Buffer& buffer, const BufferBarrierDesc& desc) noexcept
		{
			const auto& bufferDesc = buffer.GetDesc();

			D3D12_BUFFER_BARRIER bufferBarrier = {};

			bufferBarrier.SyncBefore   = ConvertBarrierSync(desc.syncBefore);
			bufferBarrier.AccessBefore = ConvertBarrierAccess(desc.accessBefore);

			bufferBarrier.SyncAfter   = ConvertBarrierSync(desc.syncAfter);
			bufferBarrier.AccessAfter = ConvertBarrierAccess(desc.accessAfter);

			bufferBarrier.pResource = buffer.GetD3D12Resource();
			bufferBarrier.Offset    = 0;
			bufferBarrier.Size      = bufferDesc.byteSize;

			return bufferBarrier;
		}

		D3D12_TEXTURE_BARRIER
		MakeTextureBarrier(const Texture& texture, const TextureBarrierDesc& desc) noexcept
		{
			D3D12_TEXTURE_BARRIER textureBarrier = {};

			textureBarrier.SyncBefore   = ConvertBarrierSync(desc.syncBefore);
			textureBarrier.AccessBefore = ConvertBarrierAccess(desc.accessBefore);

			textureBarrier.SyncAfter   = ConvertBarrierSync(desc.syncAfter);
			textureBarrier.AccessAfter = ConvertBarrierAccess(desc.accessAfter);

			textureBarrier.LayoutBefore = ConvertBarrierLayout(desc.layoutBefore);
			textureBarrier.LayoutAfter  = ConvertBarrierLayout(desc.layoutAfter);

			textureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

			textureBarrier.pResource = texture.GetD3D12Resource();

			if (desc.mipCount == uint32_t(-1) && desc.layerCount == uint32_t(-1))
			{
				textureBarrier.Subresources.IndexOrFirstMipLevel =
					D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			}
			else
			{
				textureBarrier.Subresources.IndexOrFirstMipLevel = desc.baseMipLevel;
				textureBarrier.Subresources.NumMipLevels         = desc.mipCount;
				textureBarrier.Subresources.FirstArraySlice      = desc.baseArrayLayer;
				textureBarrier.Subresources.NumArraySlices       = desc.layerCount;
				textureBarrier.Subresources.FirstPlane           = desc.firstPlane;
				textureBarrier.Subresources.NumPlanes            = desc.planeCount;
			}

			return textureBarrier;
		}
	}

	void
	CommandList::Barrier(BufferHandle handle, const BufferBarrierDesc& barrier) noexcept
	{
		Barrier(
			std::span<const BufferHandle>(&handle, 1),
			std::span<const BufferBarrierDesc>(&barrier, 1));
	}

	void
	CommandList::Barrier(TextureHandle handle, const TextureBarrierDesc& barrier) noexcept
	{
		Barrier(
			std::span<const TextureHandle>(&handle, 1),
			std::span<const TextureBarrierDesc>(&barrier, 1));
	}

	void
	CommandList::Barrier(
		std::span<const BufferHandle>      handles,
		std::span<const BufferBarrierDesc> barriers) noexcept
	{
		gassert(handles.size() == barriers.size(), "Barrier handle/desc spans must match in size");
		if (handles.empty())
		{
			return;
		}

		std::vector<D3D12_BUFFER_BARRIER> bufferBarriers;
		bufferBarriers.reserve(handles.size());

		for (size_t i = 0; i < handles.size(); ++i)
		{
			bufferBarriers.push_back(
				MakeBufferBarrier(m_ResourceManager->GetBuffer(handles[i]), barriers[i]));
		}

		D3D12_BARRIER_GROUP group = {};
		group.Type                = D3D12_BARRIER_TYPE_BUFFER;
		group.NumBarriers         = static_cast<uint32_t>(bufferBarriers.size());
		group.pBufferBarriers     = bufferBarriers.data();

		m_CommandList->Barrier(1, &group);
	}

	void
	CommandList::Barrier(
		std::span<const TextureHandle>      handles,
		std::span<const TextureBarrierDesc> barriers) noexcept
	{
		gassert(handles.size() == barriers.size(), "Barrier handle/desc spans must match in size");
		if (handles.empty())
		{
			return;
		}

		std::vector<D3D12_TEXTURE_BARRIER> textureBarriers;
		textureBarriers.reserve(handles.size());

		for (size_t i = 0; i < handles.size(); ++i)
		{
			textureBarriers.push_back(
				MakeTextureBarrier(m_ResourceManager->GetTexture(handles[i]), barriers[i]));
		}

		D3D12_BARRIER_GROUP group = {};
		group.Type                = D3D12_BARRIER_TYPE_TEXTURE;
		group.NumBarriers         = static_cast<uint32_t>(textureBarriers.size());
		group.pTextureBarriers    = textureBarriers.data();

		m_CommandList->Barrier(1, &group);
	}

	void
	CommandList::Barrier(RtvHandle handle, const TextureBarrierDesc& barrier) noexcept
	{
		auto& rtv           = m_ResourceManager->GetRtv(handle);
		auto  textureHandle = rtv.GetTextureHandle();

		gassert(m_ResourceManager->ValidRtvHandle(handle), "RTV has invalid texture handle");
		Barrier(textureHandle, barrier);
	}

	void
	CommandList::Barrier(DsvHandle handle, const TextureBarrierDesc& barrier) noexcept
	{
		auto& rtv           = m_ResourceManager->GetDsv(handle);
		auto  textureHandle = rtv.GetTextureHandle();

		gassert(m_ResourceManager->ValidDsvHandle(handle), "DSV has invalid texture handle");
		Barrier(textureHandle, barrier);
	}

	void
	CommandList::SetMeshletState(const MeshletState& gfxState) noexcept
	{
		m_CurrentMeshletState = gfxState;
	}

	void
	CommandList::ApplyMeshletState() noexcept
	{
		gassert(m_CurrentMeshletState.has_value(), "Graphics state must be set before drawing");
		gassert(
			m_CurrentMeshletState->kernel != nullptr &&
				m_CurrentMeshletState->kernel->pipeline.IsInitialized(),
			"Meshlet kernel must be set in graphics state");

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

			D3D12_CPU_DESCRIPTOR_HANDLE  dsvCpuHandle  = {};
			D3D12_CPU_DESCRIPTOR_HANDLE* pDsvCpuHandle = nullptr;

			auto depthAttachment = m_CurrentMeshletState->frameBuffer.depthAttachment;
			if (!depthAttachment.IsNull())
			{
				auto& dsv     = m_ResourceManager->GetDsv(depthAttachment);
				dsvCpuHandle  = dsv.GetCpuHandle();
				pDsvCpuHandle = &dsvCpuHandle;
			}

			m_CommandList->OMSetRenderTargets(
				static_cast<UINT>(rtvs.size()),
				d3d12RenderTargets.data(),
				FALSE,
				pDsvCpuHandle);
		}

		// Depth Stencil

		// Pipeline state
		{
			auto* pipeline = m_CurrentMeshletState->kernel->pipeline->As<MeshletPipeline>();

			m_CommandList->SetPipelineState(pipeline->GetPipelineState());
			m_CommandList->SetGraphicsRootSignature(pipeline->GetRootSignature());

			const MeshletKernel& kernel = *m_CurrentMeshletState->kernel;

			for (const auto& [name, uniforms] : kernel.uniforms)
			{
				BindUniforms(uniforms, /*compute*/ false);
			}

#if defined(BERNINI_GPU_DEBUG)
			if (auto it = kernel.uniforms.find("gDebug");
			    it != kernel.uniforms.end() && !m_ActiveDebugBuffer.IsNull())
			{
				const DescriptorHandle handle(m_ActiveDebugBuffer.slot);
				BindConstantData(
					&handle,
					sizeof(handle),
					it->second.GetRootParamIndex(),
					/*compute*/ false);
			}
#endif
		}
	}

	void
	CommandList::DispatchMesh(
		uint32_t threadGroupCountX,
		uint32_t threadGroupCountY,
		uint32_t threadGroupCountZ) noexcept
	{
		ApplyMeshletState();

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
	CommandList::DispatchMeshIndirect(uint32_t argIdx) noexcept
	{
		ApplyMeshletState();

		gassert(
			!m_CurrentMeshletState->indirectArgs.IsNull(),
			"MeshletState.indirectArgs must be set for DispatchMeshIndirect");

		const auto& argsBuffer = m_ResourceManager->GetBuffer(m_CurrentMeshletState->indirectArgs);

		m_CommandList->ExecuteIndirect(
			m_MeshDispatchSig.Get(),
			1,
			argsBuffer.GetD3D12Resource(),
			static_cast<UINT64>(argIdx) * sizeof(D3D12_DISPATCH_MESH_ARGUMENTS),
			nullptr,
			0);
	}

	void
	CommandList::SetComputeState(const ComputeState& computeState) noexcept
	{
		m_CurrentComputeState = computeState;
	}

#if defined(BERNINI_GPU_DEBUG)
	void
	CommandList::SetActiveDebugBuffer(BufferHandle handle) noexcept
	{
		m_ActiveDebugBuffer = handle;
	}
#endif

	void
	CommandList::Dispatch(
		uint32_t threadGroupCountX,
		uint32_t threadGroupCountY,
		uint32_t threadGroupCountZ) noexcept
	{
		gassert(m_CurrentComputeState.has_value(), "Compute state must be set before dispatch");
		gassert(
			m_CurrentComputeState->kernel != nullptr &&
				m_CurrentComputeState->kernel->pipeline.IsInitialized(),
			"Compute kernel must be set in compute state");

		auto* pipeline = m_CurrentComputeState->kernel->pipeline->As<ComputePipeline>();

		m_CommandList->SetPipelineState(pipeline->GetPipelineState());
		m_CommandList->SetComputeRootSignature(pipeline->GetRootSignature());

		const ComputeKernel& kernel = *m_CurrentComputeState->kernel;

		for (const auto& [name, uniforms] : kernel.uniforms)
		{
			BindUniforms(uniforms, /*compute*/ true);
		}

#if defined(BERNINI_GPU_DEBUG)
		// Auto-bind the active GPU-assertion buffer into the shader's implicit gDebug
		// cbuffer, overriding the (zeroed) reflected binding. No-op when the kernel
		// reflected no gDebug (the shader never calls dbg_raise) or none is active.
		// The kernel is const during dispatch, so we bind the CBV from the handle
		// directly rather than mutating the kernel's uniform mirror.
		if (auto it = kernel.uniforms.find("gDebug");
		    it != kernel.uniforms.end() && !m_ActiveDebugBuffer.IsNull())
		{
			const DescriptorHandle handle(m_ActiveDebugBuffer.slot);
			BindConstantData(
				&handle,
				sizeof(handle),
				it->second.GetRootParamIndex(),
				/*compute*/ true);
		}
#endif

		m_CommandList->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	}

	void
	CommandList::BindConstantData(
		const void* data,
		size_t      size,
		uint32_t    rootParamIndex,
		bool        compute) noexcept
	{
		if (size == 0)
			return;

		size_t                    offsetInUploadBuffer = 0;
		void*                     cpuVA                = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS gpuVA                = 0;

		auto success = m_UploadManager.SuballocateBuffer(
			m_LastCompletedFence,
			size,
			nullptr,
			m_CurrentUploadBuffer,
			&offsetInUploadBuffer,
			&cpuVA,
			&gpuVA,
			m_RecordingVersion,
			D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

		gassert(success, "Failed to suballocate constant buffer");

		memcpy(cpuVA, data, size);

		if (compute)
		{
			m_CommandList->SetComputeRootConstantBufferView(rootParamIndex, gpuVA);
		}
		else
		{
			m_CommandList->SetGraphicsRootConstantBufferView(rootParamIndex, gpuVA);
		}
	}

	void
	CommandList::BindUniforms(const Uniforms& uniforms, bool compute) noexcept
	{
		BindConstantData(
			uniforms.Data(),
			uniforms.GetSize(),
			uniforms.GetRootParamIndex(),
			compute);
	}

	void
	CommandList::SubmitChunks(ICommandQueue* cmdQueue) noexcept
	{
		gassert(cmdQueue != nullptr, "Command queue cannot be null");

		auto submittedVersion = MakeVersion(cmdQueue->GetNextFenceValue(), m_Desc.type, true);
		m_UploadManager.SubmitChunks(m_RecordingVersion, submittedVersion);
		m_RecordingVersion = 0;
	}

}
