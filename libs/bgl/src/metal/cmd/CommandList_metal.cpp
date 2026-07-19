#include "cmd/CommandList_metal.h"

#include "cmd/CommandQueue_metal.h"
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

		NS::AutoreleasePool* pool     = NS::AutoreleasePool::alloc()->init();
		auto*                mtlQueue = cmdQueue->As<CommandQueue>()->GetMTLCommandQueue();

		m_StagingBuffers.clear();
		m_CmdBuffer = NS::RetainPtr(mtlQueue->commandBuffer());
		m_Open      = true;

		pool->release();
	}

	void
	CommandList::Close() noexcept
	{
		gassert(m_Open, "Command list is not open");
		m_Open = false;
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

		auto* dst = m_ResourceManager->GetBuffer(handle).Handle();

		// A private buffer cannot be written from the CPU; stage the bytes in a shared buffer and blit
		// them across on the GPU timeline, so the write orders ahead of a later readback copy.
		auto staging =
			NS::TransferPtr(m_Device->newBuffer(data, byteSize, MTL::ResourceStorageModeShared));

		NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
		auto*                blit = m_CmdBuffer->blitCommandEncoder();
		blit->copyFromBuffer(staging.get(), 0, dst, gpuBufferOffset, byteSize);
		blit->endEncoding();
		pool->release();

		m_StagingBuffers.push_back(std::move(staging));
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
		auto*       dstBuffer = m_ResourceManager->GetReadbackBuffer(dst).Handle();

		NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
		auto*                blit = m_CmdBuffer->blitCommandEncoder();
		blit->copyFromBuffer(srcBuffer.Handle(), 0, dstBuffer, 0, srcBuffer.ByteSize());
		blit->endEncoding();
		pool->release();
	}

	void
	CommandList::BeginEvent(std::string_view name) noexcept
	{
		NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
		m_CmdBuffer->pushDebugGroup(
			NS::String::string(std::string(name).c_str(), NS::UTF8StringEncoding));
		pool->release();
	}

	void
	CommandList::EndEvent() noexcept
	{
		m_CmdBuffer->popDebugGroup();
	}
}
