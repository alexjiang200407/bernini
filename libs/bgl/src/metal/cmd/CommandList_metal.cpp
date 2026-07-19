#include "cmd/CommandList_metal.h"

#include "cmd/CommandQueue_metal.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline_metal.h"
#include "pipeline/MeshletKernel.h"
#include "pipeline/MeshletPipeline_metal.h"
#include "resource/ResourceManager_metal.h"

namespace bgl
{
	namespace
	{
		struct PatchedUniform
		{
			std::vector<std::byte>    bytes;
			std::vector<MTL::Buffer*> resident;  // buffers the encoder must make resident
		};

		// D3D12 writes a bindless handle's slot index into the cbuffer and a directly-indexed heap
		// resolves it in-shader; Metal has no such heap, so at dispatch each handle field is rewritten
		// from its slot index to the buffer's gpuAddress (what the emitted MSL dereferences). Copies
		// the mirror, does that rewrite, and returns the buffers to make resident. Compute and mesh.
		PatchedUniform
		PatchUniformHandles(
			const Uniforms&              uniforms,
			const std::vector<uint32_t>& handleOffsets,
			ResourceManager*             rm)
		{
			PatchedUniform result;
			const size_t   size = uniforms.GetSize();
			result.bytes.resize(size);
			std::memcpy(result.bytes.data(), uniforms.Data(), size);

			for (uint32_t offset : handleOffsets)
			{
				uint32_t slotIndex = 0;
				std::memcpy(&slotIndex, result.bytes.data() + offset, sizeof(uint32_t));

				MTL::Buffer*   buffer = rm->GetBufferBySlotIndex(slotIndex);
				const uint64_t addr   = buffer->gpuAddress();
				std::memcpy(result.bytes.data() + offset, &addr, sizeof(uint64_t));
				result.resident.push_back(buffer);
			}
			return result;
		}
	}

	CommandList::CommandList(
		const CommandListDesc& desc,
		ICommandAllocator*,
		ResourceManagerRef resourceManager) :
		m_Desc(desc), m_ResourceManager(std::move(resourceManager))
	{
		gassert(m_ResourceManager != nullptr, "Resource manager cannot be null");
		m_Device = m_ResourceManager->As<ResourceManager>()->GetMTLDevice();
	}

	void
	CommandList::Open(ICommandQueue* cmdQueue, ICommandAllocator*) noexcept
	{
		gassert(cmdQueue != nullptr, "Command queue cannot be null");
		gassert(!m_Open, "Command list is already open");

		// One pool for the whole open scope: every encoder and temporary autoreleased between Open and
		// Close drains here. The command buffer outlives the drain via its own retain (RetainPtr).
		m_ScopePool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());

		auto* mtlQueue = cmdQueue->As<CommandQueue>()->GetMTLCommandQueue();
		m_CmdBuffer    = NS::RetainPtr(mtlQueue->commandBuffer());
		m_Open         = true;
	}

	void
	CommandList::Close() noexcept
	{
		gassert(m_Open, "Command list is not open");
		m_Open = false;
		m_ScopePool.reset();
	}

	void
	CommandList::WriteBuffer(
		BufferHandle handle,
		const void*  data,
		size_t       gpuBufferOffset,
		size_t       byteSize) noexcept
	{
		gassert(m_Open, "WriteBuffer on a closed command list");
		gassert(m_ResourceManager->ValidBufferHandle(handle), "WriteBuffer on an invalid handle");

		auto* dst = m_ResourceManager->GetBuffer(handle).GetMTLResource();

		// A private buffer cannot be written from the CPU; stage the bytes in a shared buffer and blit
		// them across on the GPU timeline, so the write orders ahead of a later readback copy. The
		// command buffer retains the staging buffer until it completes, so it needs no separate owner.
		auto staging =
			NS::TransferPtr(m_Device->newBuffer(data, byteSize, MTL::ResourceStorageModeShared));

		auto* blit = m_CmdBuffer->blitCommandEncoder();
		blit->copyFromBuffer(staging.get(), 0, dst, gpuBufferOffset, byteSize);
		blit->endEncoding();
	}

	void
	CommandList::CopyBufferToReadback(ReadbackBufferHandle dst, BufferHandle src) noexcept
	{
		gassert(m_Open, "CopyBufferToReadback on a closed command list");
		gassert(m_ResourceManager->ValidBufferHandle(src), "CopyBufferToReadback: invalid source");
		gassert(
			m_ResourceManager->ValidReadbackBufferHandle(dst),
			"CopyBufferToReadback: invalid destination");

		const auto& srcBuffer = m_ResourceManager->GetBuffer(src);
		auto*       dstBuffer = m_ResourceManager->GetReadbackBuffer(dst).GetMTLResource();

		auto* blit = m_CmdBuffer->blitCommandEncoder();
		blit->copyFromBuffer(
			srcBuffer.GetMTLResource(),
			0,
			dstBuffer,
			0,
			srcBuffer.GetDesc().byteSize);
		blit->endEncoding();
	}

	void
	CommandList::CopyTextureToReadback(ReadbackBufferHandle dst, TextureHandle src) noexcept
	{
		gassert(m_Open, "CopyTextureToReadback on a closed command list");
		gassert(
			m_ResourceManager->ValidTextureHandle(src),
			"CopyTextureToReadback: invalid source");
		gassert(
			m_ResourceManager->ValidReadbackBufferHandle(dst),
			"CopyTextureToReadback: invalid destination");

		const Texture&              tex    = m_ResourceManager->GetTexture(src);
		const TextureDesc&          desc   = tex.GetDesc();
		const TextureReadbackLayout layout = m_ResourceManager->GetTextureReadbackLayout(src);
		MTL::Buffer* dstBuffer = m_ResourceManager->GetReadbackBuffer(dst).GetMTLResource();

		auto* blit = m_CmdBuffer->blitCommandEncoder();
		blit->copyFromTexture(
			tex.GetMTLResource(),
			0,
			0,
			MTL::Origin(0, 0, 0),
			MTL::Size(desc.width, desc.height, 1),
			dstBuffer,
			layout.offset,
			layout.rowPitch,
			layout.totalBytes);
		blit->endEncoding();
	}

	void
	CommandList::ClearRenderTarget(MTL::Texture* texture, const float clearVal[4]) noexcept
	{
		gassert(m_Open, "ClearRenderTarget on a closed command list");

		MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
		MTL::RenderPassColorAttachmentDescriptor* color = pass->colorAttachments()->object(0);
		color->setTexture(texture);
		color->setLoadAction(MTL::LoadActionClear);
		color->setStoreAction(MTL::StoreActionStore);
		color->setClearColor(
			MTL::ClearColor::Make(clearVal[0], clearVal[1], clearVal[2], clearVal[3]));

		// An empty pass: the Clear load action writes the color and Store keeps it.
		m_CmdBuffer->renderCommandEncoder(pass)->endEncoding();
	}

	void
	CommandList::BeginEvent(std::string_view name) noexcept
	{
		m_CmdBuffer->pushDebugGroup(
			NS::String::string(std::string(name).c_str(), NS::UTF8StringEncoding));
	}

	void
	CommandList::EndEvent() noexcept
	{
		m_CmdBuffer->popDebugGroup();
	}

	void
	CommandList::SetMeshletState(const MeshletState& gfxState) noexcept
	{
		m_MeshletState = gfxState;
	}

	void
	CommandList::DispatchMesh(uint32_t x, uint32_t y, uint32_t z) noexcept
	{
		gassert(m_Open, "DispatchMesh on a closed command list");
		gassert(m_MeshletState.kernel != nullptr, "DispatchMesh without a meshlet state");

		auto* pipeline = m_MeshletState.kernel->pipeline->As<MeshletPipeline>();
		auto* rm       = m_ResourceManager->As<ResourceManager>();

		// Describe the render pass from the framebuffer. Load-preserve: clears are their own pass, so
		// prior contents (e.g. an earlier ClearRtv) survive into this draw.
		MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
		const FrameBuffer&         fb   = m_MeshletState.frameBuffer;
		for (size_t i = 0; i < fb.colorAttachments.size(); ++i)
		{
			MTL::Texture* texture =
				rm->GetTexture(rm->GetRtvTexture(fb.colorAttachments[i])).GetMTLResource();
			MTL::RenderPassColorAttachmentDescriptor* c = pass->colorAttachments()->object(i);
			c->setTexture(texture);
			c->setLoadAction(MTL::LoadActionLoad);
			c->setStoreAction(MTL::StoreActionStore);
		}

		MTL::RenderCommandEncoder* enc = m_CmdBuffer->renderCommandEncoder(pass);
		enc->setRenderPipelineState(pipeline->GetMTLPipelineState());

		// A cbuffer reflects at one buffer index that every stage using it shares, so bind each to all
		// stages present (an unused bind is ignored). setBytes caps at 4KB, which cbuffers stay under.
		const bool hasObject     = m_MeshletState.kernel->pipeline->GetDesc().ampShader != nullptr;
		MTL::RenderStages stages = MTL::RenderStageMesh | MTL::RenderStageFragment;
		if (hasObject)
			stages |= MTL::RenderStageObject;

		for (const auto& [name, uniforms] : m_MeshletState.kernel->uniforms)
		{
			const UniformLayoutEntry entry = pipeline->GetUniformLayoutEntry(name);
			const PatchedUniform     patched =
				PatchUniformHandles(uniforms, pipeline->GetHandleOffsets(name), rm);

			for (MTL::Buffer* buffer : patched.resident)
				enc->useResource(buffer, MTL::ResourceUsageRead | MTL::ResourceUsageWrite, stages);

			const void*  bytes = patched.bytes.data();
			const size_t size  = patched.bytes.size();
			enc->setMeshBytes(bytes, size, entry.rootParamIndex);
			enc->setFragmentBytes(bytes, size, entry.rootParamIndex);
			if (hasObject)
				enc->setObjectBytes(bytes, size, entry.rootParamIndex);
		}

		// Single viewport/scissor only; multi-viewport (setViewports + a viewport-index shader) lands
		// if a pass ever needs it.
		const auto& viewports = m_MeshletState.viewportState.viewports;
		if (!viewports.empty())
		{
			const Viewport& vp = viewports.front();
			enc->setViewport(
				MTL::Viewport{ vp.minX,
			                   vp.minY,
			                   vp.maxX - vp.minX,
			                   vp.maxY - vp.minY,
			                   vp.minZ,
			                   vp.maxZ });
		}
		const auto& scissors = m_MeshletState.viewportState.scissorRects;
		if (!scissors.empty())
		{
			const Rect& r = scissors.front();
			enc->setScissorRect(
				MTL::ScissorRect{ static_cast<NS::UInteger>(r.minX),
			                      static_cast<NS::UInteger>(r.minY),
			                      static_cast<NS::UInteger>(r.maxX - r.minX),
			                      static_cast<NS::UInteger>(r.maxY - r.minY) });
		}

		enc->drawMeshThreadgroups(
			MTL::Size(x, y, z),
			pipeline->GetThreadsPerObjectThreadgroup(),
			pipeline->GetThreadsPerMeshThreadgroup());
		enc->endEncoding();
	}

	void
	CommandList::SetComputeState(const ComputeState& computeState) noexcept
	{
		m_ComputeState = computeState;
	}

	void
	CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) noexcept
	{
		gassert(m_Open, "Dispatch on a closed command list");
		gassert(m_ComputeState.kernel != nullptr, "Dispatch without a compute state");

		auto* pipeline = m_ComputeState.kernel->pipeline->As<ComputePipeline>();
		auto* rm       = m_ResourceManager->As<ResourceManager>();

		auto* enc = m_CmdBuffer->computeCommandEncoder();
		enc->setComputePipelineState(pipeline->GetMTLPipelineState());

		for (const auto& [name, uniforms] : m_ComputeState.kernel->uniforms)
		{
			const UniformLayoutEntry entry = pipeline->GetUniformLayoutEntry(name);
			const PatchedUniform     patched =
				PatchUniformHandles(uniforms, pipeline->GetHandleOffsets(name), rm);

			for (MTL::Buffer* buffer : patched.resident)
				enc->useResource(buffer, MTL::ResourceUsageRead | MTL::ResourceUsageWrite);

			enc->setBytes(patched.bytes.data(), patched.bytes.size(), entry.rootParamIndex);
		}

		enc->dispatchThreadgroups(MTL::Size(x, y, z), pipeline->GetThreadsPerThreadgroup());
		enc->endEncoding();
	}
}
