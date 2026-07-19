#include "cmd/CommandList_metal.h"

#include "cmd/CommandQueue_metal.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline_metal.h"
#include "resource/ResourceManager_metal.h"

namespace bgl
{
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
			const size_t             size  = uniforms.GetSize();

			// The shared mirror carries each handle's slot.index; Metal wants the buffer's gpuAddress
			// there. Patch a copy, make each referenced buffer resident, then bind the copy at the
			// kernel's [[buffer(rootParamIndex)]] slot.
			std::vector<std::byte> patched(size);
			std::memcpy(patched.data(), uniforms.Data(), size);

			for (uint32_t offset : pipeline->GetHandleOffsets(name))
			{
				uint32_t slotIndex = 0;
				std::memcpy(&slotIndex, patched.data() + offset, sizeof(uint32_t));

				MTL::Buffer*   buffer = rm->GetBufferBySlotIndex(slotIndex);
				const uint64_t addr   = buffer->gpuAddress();
				std::memcpy(patched.data() + offset, &addr, sizeof(uint64_t));

				enc->useResource(buffer, MTL::ResourceUsageRead | MTL::ResourceUsageWrite);
			}

			enc->setBytes(patched.data(), size, entry.rootParamIndex);
		}

		enc->dispatchThreadgroups(MTL::Size(x, y, z), pipeline->GetThreadsPerThreadgroup());
		enc->endEncoding();
	}
}
