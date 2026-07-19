#include "resource/ResourceManager_metal.h"

#include "cmd/CommandList_metal.h"
#include "util_metal.h"

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

	TextureHandle
	ResourceManager::CreateTexture(const TextureDesc& desc) noexcept
	{
		const auto slot = m_Textures.try_allocate_and_emplace(m_Device, desc);
		if (slot.is_null())
		{
			logger::error("CreateTexture '{}': texture pool exhausted", desc.debugName);
			return TextureHandle{};
		}
		return TextureHandle{ slot, desc.usage };
	}

	RtvHandle
	ResourceManager::CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept
	{
		gassert(ValidTextureHandle(textureHandle), "CreateRtv on an invalid texture");
		const auto slot = m_Rtvs.try_allocate_and_emplace(desc, textureHandle);
		if (slot.is_null())
		{
			logger::error("CreateRtv '{}': RTV pool exhausted", desc.debugName);
			return RtvHandle{};
		}
		return RtvHandle{ slot.index, slot.generation };
	}

	void
	ResourceManager::DestroyTexture(TextureHandle handle, uint64_t, bool) noexcept
	{
		gassert(ValidTextureHandle(handle), "Cannot destroy invalid texture handle");
		m_Textures.release_slot(handle.slot);
	}

	void
	ResourceManager::DestroyTexture(TextureHandle handle) noexcept
	{
		DestroyTexture(handle, 0, false);
	}

	void
	ResourceManager::DestroyRtv(RtvHandle handle, uint64_t, bool) noexcept
	{
		gassert(ValidRtvHandle(handle), "Cannot destroy invalid RTV handle");
		m_Rtvs.release_slot(handle.idx);
	}

	const Texture&
	ResourceManager::GetTexture(TextureHandle handle) const noexcept
	{
		return m_Textures[handle.slot];
	}

	const Rtv&
	ResourceManager::GetRtv(RtvHandle handle) const noexcept
	{
		return m_Rtvs[handle.idx];
	}

	TextureHandle
	ResourceManager::GetRtvTexture(RtvHandle handle) const noexcept
	{
		return GetRtv(handle).GetTextureHandle();
	}

	TextureReadbackLayout
	ResourceManager::GetTextureReadbackLayout(TextureHandle handle) const noexcept
	{
		const TextureDesc& desc = GetTexture(handle).GetDesc();

		// Metal has no 256-byte row-pitch rule (unlike D3D12), so the rows pack tight.
		TextureReadbackLayout layout;
		layout.rowSizeBytes = static_cast<uint64_t>(desc.width) * FormatBytesPerPixel(desc.format);
		layout.rowPitch     = layout.rowSizeBytes;
		layout.rowCount     = desc.height;
		layout.offset       = 0;
		layout.totalBytes   = layout.rowPitch * desc.height;
		return layout;
	}

	bool
	ResourceManager::ValidTextureHandle(const TextureHandle& handle) const noexcept
	{
		return !handle.IsNull() && m_Textures.valid(handle.slot) &&
		       !m_Textures[handle.slot].IsNull();
	}

	bool
	ResourceManager::ValidRtvHandle(const RtvHandle& handle) const noexcept
	{
		return !handle.IsNull() && m_Rtvs.valid(handle.idx, handle.generation) &&
		       !m_Rtvs[handle.idx].IsNull();
	}

	void
	ResourceManager::ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4]) noexcept
	{
		gassert(ValidRtvHandle(handle), "ClearRtv on an invalid RTV handle");
		gassert(cmdList != nullptr && cmdList->IsOpen(), "ClearRtv needs an open command list");

		MTL::Texture* texture = GetTexture(GetRtv(handle).GetTextureHandle()).GetMTLResource();
		cmdList->As<CommandList>()->ClearRenderTarget(texture, clearVal);
	}
}
