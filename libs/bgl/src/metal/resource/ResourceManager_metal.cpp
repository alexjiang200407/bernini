#include "resource/ResourceManager_metal.h"

namespace bgl
{
	ResourceManager::ResourceManager(MTL::Device* device, const ResourceManagerDesc&) :
		m_Device(device)
	{}

	BufferHandle
	ResourceManager::CreateStructBuffer(const StructBufferDesc& desc) noexcept
	{
		gassert(desc.stride > 0, "StructuredBuffer requires a valid stride");
		gassert(desc.elementCount > 0, "StructuredBuffer requires a valid element count");

		BufferDesc bufferDesc;
		bufferDesc.byteSize  = static_cast<uint64_t>(desc.stride) * desc.elementCount;
		bufferDesc.isUav     = desc.isUav;
		bufferDesc.debugName = desc.debugName;

		const auto slot = m_Buffers.try_allocate_and_emplace(m_Device, bufferDesc);
		if (slot.is_null())
		{
			logger::error("CreateStructBuffer '{}': buffer pool exhausted", desc.debugName);
			return BufferHandle{};
		}
		return BufferHandle{ slot };
	}

	BufferHandle
	ResourceManager::CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept
	{
		// A compute buffer is a GPU-only structured buffer with UAV access; reuse the
		// structured-buffer path to create it.
		StructBufferDesc sbDesc;
		sbDesc.stride       = desc.elementSize;
		sbDesc.elementCount = desc.maxCount;
		sbDesc.isUav        = true;
		sbDesc.debugName    = desc.debugName;
		return CreateStructBuffer(sbDesc);
	}

	ReadbackBufferHandle
	ResourceManager::CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept
	{
		const auto slot = m_Readbacks.try_allocate_and_emplace(m_Device, desc);
		if (slot.is_null())
		{
			logger::error("CreateReadbackBuffer '{}': readback pool exhausted", desc.debugName);
			return ReadbackBufferHandle{};
		}
		return ReadbackBufferHandle{ slot };
	}

	void
	ResourceManager::DestroyBuffer(BufferHandle handle, uint64_t, bool) noexcept
	{
		gassert(ValidBufferHandle(handle), "Cannot destroy invalid buffer handle");
		// The Metal buffer's storage is released by the slot's SharedPtr; a caller only reaches here
		// after WaitForFenceCPUBlocking, so an immediate free is safe.
		m_Buffers.release_slot(handle.slot);
	}

	void
	ResourceManager::DestroyReadbackBuffer(ReadbackBufferHandle handle, uint64_t, bool) noexcept
	{
		gassert(ValidReadbackBufferHandle(handle), "Cannot destroy invalid readback handle");
		m_Readbacks.release_slot(handle.slot);
	}

	void
	ResourceManager::CleanupExpiredResources(uint64_t) noexcept
	{
		// Destruction is immediate in this slice (callers wait on the fence first), so nothing is
		// pending; the deferred-by-fence path lands with the render loop.
	}

	const Buffer&
	ResourceManager::GetBuffer(BufferHandle handle) const noexcept
	{
		return m_Buffers[handle.slot];
	}

	const ReadbackBuffer&
	ResourceManager::GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept
	{
		return m_Readbacks[handle.slot];
	}

	const void*
	ResourceManager::MapReadback(ReadbackBufferHandle handle) noexcept
	{
		return m_Readbacks[handle.slot].GetData();
	}

	void
	ResourceManager::UnmapReadback(ReadbackBufferHandle) noexcept
	{
		// Shared storage on unified memory: contents() stays valid, nothing to unmap.
	}

	bool
	ResourceManager::ValidBufferHandle(const BufferHandle& handle) const noexcept
	{
		return !handle.IsNull() && m_Buffers.valid(handle.slot);
	}

	bool
	ResourceManager::ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept
	{
		return !handle.IsNull() && m_Readbacks.valid(handle.slot);
	}
}
