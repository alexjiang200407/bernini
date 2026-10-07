#include "resource/ResourceManager_vulkan.h"
#include "native_device_vulkan.h"
#include "resource/BoundedPool.h"
#include "resource/BufferMemory_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <algorithm>
#include <bgpu/GpuContext.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/NativeBufferDesc.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Sampler.h>
#include <bgpu/resource/Srv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/NativeObject.h>
#include <core/containers/slot_handle.h>
#include <core/containers/slot_vector.h>
#include <core/containers/static_vector.h>
#include <core/err/util.h>
#include <core/ref/SharedRef.h>
#include <cstdint>
#include <exception>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string_view>
#include <utility>

namespace bgpu
{
	ResourceManager::ResourceManager(GpuContextRef context, const ResourceManagerDesc& desc) :
		m_Context(std::move(context)), m_Desc(desc),
		m_Table(GetVulkanHandles(*m_Context).device, desc.maxCbvSrvUavs),
		m_Buffers(desc.maxBuffers), m_BufferSrvs(desc.maxBufferSrvs),
		m_BufferUavs(desc.maxBufferUavs), m_ReadbackBuffers(desc.maxReadbackBuffers)
	{
		core::ensure(desc.maxBuffers > 0, "maxBuffers must be greater than zero");
		// The +1 is the unbound sentinel the table burns at index 0 and never hands out.
		core::ensure(
			desc.maxCbvSrvUavs >=
				desc.maxBuffers + desc.maxSrvs + desc.maxBufferSrvs + desc.maxBufferUavs + 1,
			"maxCbvSrvUavs must cover every buffer, SRV and second view, and the unbound slot");
	}

	ResourceManager::~ResourceManager() noexcept { spdlog::trace("~ResourceManager"); }

	BufferHandle
	ResourceManager::AddBuffer(
		core::SharedRef<BufferMemory> memory,
		BufferDesc                    desc,
		const bool                    tracked) noexcept
	{
		const auto slot = TryAllocateBounded(m_Buffers);
		if (slot.is_null())
		{
			spdlog::error("Creating buffer '{}': buffer pool exhausted", desc.debugName);
			return BufferHandle{};
		}

		uint32_t descriptorIndex = 0xFFFFFFFF;
		try
		{
			descriptorIndex = m_Table.Allocate();
		}
		catch (const std::exception& e)
		{
			spdlog::error("Creating buffer '{}': {}", desc.debugName, e.what());
			m_Buffers.release_slot(slot.index);
			return BufferHandle{};
		}

		m_Table.WriteBuffer(descriptorIndex, memory->GetVkBuffer());
		m_Buffers[slot] = Buffer(std::move(memory), descriptorIndex, std::move(desc), tracked);
		return BufferHandle{ slot, descriptorIndex };
	}

	BufferHandle
	ResourceManager::CreateDeviceBuffer(BufferDesc desc) noexcept
	{
		// Out of device memory throws out of BufferMemory, and this is noexcept: caught here, or a
		// recoverable failure becomes a terminate.
		auto memory = core::SharedRef<BufferMemory>();
		try
		{
			memory = core::SharedRef<BufferMemory>::Make(
				m_Context,
				desc.byteSize,
				BufferMemoryKind::kDevice,
				desc.debugName);
		}
		catch (const std::exception& e)
		{
			spdlog::error(
				"Creating buffer '{}': allocating {} bytes of device memory failed: {}",
				desc.debugName,
				desc.byteSize,
				e.what());
			return BufferHandle{};
		}

		const std::lock_guard lock(m_PoolMutex);
		return AddBuffer(std::move(memory), std::move(desc), true);
	}

	BufferHandle
	ResourceManager::CreateStructBuffer(const StructBufferDesc& desc) noexcept
	{
		core::ensure(desc.stride > 0, "StructuredBuffer requires a valid structural stride");
		core::ensure(desc.elementCount > 0, "StructuredBuffer requires a valid element count");

		auto bufferDesc      = BufferDesc();
		bufferDesc.byteSize  = static_cast<uint64_t>(desc.stride) * desc.elementCount;
		bufferDesc.isUav     = desc.isUav;
		bufferDesc.allowsUav = desc.allowsUav;
		bufferDesc.debugName = desc.debugName;
		return CreateDeviceBuffer(std::move(bufferDesc));
	}

	BufferHandle
	ResourceManager::CreateComputeBuffer(const ComputeBufferDesc& desc) noexcept
	{
		core::ensure(desc.initialCount > 0, "ComputeBuffer requires a positive element count");
		core::ensure(desc.elementSize > 0, "ComputeBuffer requires a positive element size");

		auto structDesc         = StructBufferDesc();
		structDesc.stride       = desc.elementSize;
		structDesc.elementCount = desc.initialCount;
		structDesc.isUav        = true;
		structDesc.debugName    = desc.debugName;
		return CreateStructBuffer(structDesc);
	}

	BufferHandle
	ResourceManager::CreateRawBuffer(const RawViewDesc& desc) noexcept
	{
		core::ensure(desc.byteSize > 0, "A raw buffer requires a byte size");
		core::ensure(desc.byteSize % 4 == 0, "A raw view addresses whole 32-bit words");
		core::ensure(desc.byteSize <= c_MaxRawBufferBytes, "A raw view cannot address past 4 GiB");

		auto bufferDesc      = BufferDesc();
		bufferDesc.byteSize  = desc.byteSize;
		bufferDesc.isUav     = desc.isUav;
		bufferDesc.isRaw     = true;
		bufferDesc.debugName = desc.debugName;
		return CreateDeviceBuffer(std::move(bufferDesc));
	}

	core::slot_handle
	ResourceManager::AddView(
		core::slot_vector<uint32_t>& pool,
		const BufferHandle           buffer,
		const std::string_view       debugName,
		uint32_t&                    descriptorIndex) noexcept
	{
		const auto slot = TryAllocateBounded(pool);
		if (slot.is_null())
		{
			spdlog::error("Creating buffer view '{}': buffer view pool exhausted", debugName);
			return {};
		}

		try
		{
			descriptorIndex = m_Table.Allocate();
		}
		catch (const std::exception& e)
		{
			spdlog::error("Creating buffer view '{}': {}", debugName, e.what());
			pool.release_slot(slot.index);
			return {};
		}

		// A storage-buffer descriptor has no stride: the shader's declared type is the view.
		m_Table.WriteBuffer(descriptorIndex, m_Buffers[buffer.slot].GetVkBuffer());
		pool[slot.index] = descriptorIndex;
		return slot;
	}

	BufferSrvHandle
	ResourceManager::CreateBufferSrv(BufferHandle buffer, const BufferSrvDesc& desc) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidBufferHandle(buffer), "CreateBufferSrv on an invalid buffer");
		core::ensure(desc.stride > 0, "A structured view requires a stride");
		core::ensure(
			m_Buffers[buffer.slot].GetDesc().byteSize % desc.stride == 0,
			"A structured view must divide the buffer it views");

		uint32_t   descriptorIndex = 0xFFFFFFFF;
		const auto slot            = AddView(m_BufferSrvs, buffer, desc.debugName, descriptorIndex);
		if (slot.is_null())
			return BufferSrvHandle{};
		return BufferSrvHandle{ slot, descriptorIndex };
	}

	void
	ResourceManager::DestroyBufferSrv(BufferSrvHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidBufferSrvHandle(handle), "Cannot destroy invalid buffer view handle");

		const uint32_t descriptorIndex = m_BufferSrvs[handle.slot.index];
		if (deferred)
		{
			m_BufferSrvs.retire_slot(handle.slot.index);
			RetireDeferred(PendingType::kBufferSrv, handle.slot.index, descriptorIndex);
		}
		else
		{
			m_BufferSrvs.release_slot(handle.slot.index);
			m_Table.Free(descriptorIndex);
		}
	}

	bool
	ResourceManager::ValidBufferSrvHandle(const BufferSrvHandle& handle) const noexcept
	{
		return m_BufferSrvs.valid(handle.slot);
	}

	BufferUavHandle
	ResourceManager::CreateBufferUav(BufferHandle buffer, const BufferUavDesc& desc) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidBufferHandle(buffer), "CreateBufferUav on an invalid buffer");
		core::ensure(desc.stride > 0, "A structured view requires a stride");

		const BufferDesc& bufferDesc = m_Buffers[buffer.slot].GetDesc();
		core::ensure(
			bufferDesc.isUav || bufferDesc.allowsUav,
			"CreateBufferUav on a buffer created without allowsUav");
		core::ensure(!bufferDesc.isRaw, "CreateBufferUav on a raw buffer");
		core::ensure(
			bufferDesc.byteSize % desc.stride == 0,
			"A structured view must divide the buffer it views");

		uint32_t   descriptorIndex = 0xFFFFFFFF;
		const auto slot            = AddView(m_BufferUavs, buffer, desc.debugName, descriptorIndex);
		if (slot.is_null())
			return BufferUavHandle{};
		return BufferUavHandle{ slot, descriptorIndex };
	}

	void
	ResourceManager::DestroyBufferUav(BufferUavHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidBufferUavHandle(handle), "Cannot destroy invalid buffer view handle");

		const uint32_t descriptorIndex = m_BufferUavs[handle.slot.index];
		if (deferred)
		{
			m_BufferUavs.retire_slot(handle.slot.index);
			RetireDeferred(PendingType::kBufferUav, handle.slot.index, descriptorIndex);
		}
		else
		{
			m_BufferUavs.release_slot(handle.slot.index);
			m_Table.Free(descriptorIndex);
		}
	}

	bool
	ResourceManager::ValidBufferUavHandle(const BufferUavHandle& handle) const noexcept
	{
		return m_BufferUavs.valid(handle.slot);
	}

	NativeObject
	ResourceManager::GetNativeBuffer(BufferHandle handle, NativeObjectType type) const noexcept
	{
		if (type != NativeObjectType::kVkBuffer || !ValidBufferHandle(handle))
			return {};
		return NativeObject{ GetBuffer(handle).GetVkBuffer() };
	}

	BufferHandle
	ResourceManager::ImportNativeBuffer(const NativeBufferDesc& desc) noexcept
	{
		if (desc.type != NativeObjectType::kVkBuffer || desc.IsNull())
			return BufferHandle{};

		const uint64_t byteSize =
			static_cast<uint64_t>(desc.buffer.stride) * desc.buffer.elementCount;

		// Vulkan counts no references to a buffer, so only memory a manager made, which counts its
		// own, can be kept alive past its producer's release.
		auto memory = BufferMemory::Find(desc.object.As<VkBuffer_T>());
		if (memory == nullptr)
		{
			spdlog::error(
				"ImportNativeBuffer '{}': the buffer was not made by a bgpu resource manager",
				desc.buffer.debugName);
			return BufferHandle{};
		}
		core::ensure(
			GetVulkanHandles(*memory->GetContext()).device == GetVulkanHandles(*m_Context).device,
			"ImportNativeBuffer of another device's buffer");
		core::ensure(
			byteSize > 0 && byteSize <= memory->GetByteSize(),
			"ImportNativeBuffer views more bytes than the buffer holds");
		core::ensure(
			!desc.buffer.isUav && !desc.buffer.allowsUav,
			"An imported buffer is read-only");

		auto bufferDesc      = BufferDesc();
		bufferDesc.byteSize  = byteSize;
		bufferDesc.debugName = desc.buffer.debugName;

		const std::lock_guard lock(m_PoolMutex);
		return AddBuffer(std::move(memory), std::move(bufferDesc), false);
	}

	TextureHandle
	ResourceManager::CreateTexture(const TextureDesc& desc) noexcept
	{
		if (m_Desc.maxTextures == 0)
		{
			spdlog::error("CreateTexture '{}': texture pool exhausted", desc.debugName);
			return TextureHandle{};
		}
		NotOnVulkanYet("IResourceManager::CreateTexture");
	}

	SamplerHandle
	ResourceManager::CreateSampler(const SamplerDesc& desc) noexcept
	{
		(void)desc;
		if (m_Desc.maxSamplers == 0)
		{
			spdlog::error("CreateSampler: sampler pool exhausted");
			return SamplerHandle{};
		}
		NotOnVulkanYet("IResourceManager::CreateSampler");
	}

	ReadbackBufferHandle
	ResourceManager::CreateReadbackBuffer(const ReadbackBufferDesc& desc) noexcept
	{
		core::ensure(desc.byteSize > 0, "Readback buffer requires a positive byte size");

		auto readback = ReadbackBuffer();
		try
		{
			readback = ReadbackBuffer(m_Context, desc);
		}
		catch (const std::exception& e)
		{
			spdlog::error("CreateReadbackBuffer '{}': {}", desc.debugName, e.what());
			return ReadbackBufferHandle{};
		}

		const std::lock_guard lock(m_PoolMutex);
		const auto            slot = TryAllocateBounded(m_ReadbackBuffers);
		if (slot.is_null())
		{
			spdlog::error("CreateReadbackBuffer: readback pool exhausted");
			return ReadbackBufferHandle{};
		}
		m_ReadbackBuffers[slot.index] = std::move(readback);
		return ReadbackBufferHandle{ slot };
	}

	SrvHandle
	ResourceManager::CreateSrv(TextureHandle textureHandle, const SrvDesc& desc) noexcept
	{
		(void)textureHandle;
		(void)desc;
		if (m_Desc.maxSrvs == 0)
		{
			spdlog::error("CreateSrv: SRV pool exhausted");
			return SrvHandle{};
		}
		NotOnVulkanYet("IResourceManager::CreateSrv");
	}

	RtvHandle
	ResourceManager::CreateRtv(TextureHandle textureHandle, const RtvDesc& desc) noexcept
	{
		(void)textureHandle;
		(void)desc;
		if (m_Desc.maxRtvs == 0)
		{
			spdlog::error("CreateRtv: RTV pool exhausted");
			return RtvHandle{};
		}
		NotOnVulkanYet("IResourceManager::CreateRtv");
	}

	DsvHandle
	ResourceManager::CreateDsv(TextureHandle textureHandle, const DsvDesc& desc) noexcept
	{
		(void)textureHandle;
		(void)desc;
		if (m_Desc.maxDsvs == 0)
		{
			spdlog::error("CreateDsv: DSV pool exhausted");
			return DsvHandle{};
		}
		NotOnVulkanYet("IResourceManager::CreateDsv");
	}

	void
	ResourceManager::DestroyBuffer(BufferHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(ValidBufferHandle(handle), "Cannot destroy invalid buffer handle");

		const uint32_t descriptorIndex = m_Buffers[handle.slot].GetDescriptorIndex();
		if (deferred)
		{
			m_Buffers.retire_slot(handle.slot);
			RetireDeferred(PendingType::kBuffer, handle.slot.index, descriptorIndex);
		}
		else
		{
			m_Buffers.release_slot(handle.slot);
			m_Table.Free(descriptorIndex);
		}
	}

	void
	ResourceManager::DestroyReadbackBuffer(ReadbackBufferHandle handle, bool deferred) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(
			ValidReadbackBufferHandle(handle),
			"Cannot destroy invalid readback buffer handle");

		if (deferred)
		{
			m_ReadbackBuffers.retire_slot(handle.slot.index);
			RetireDeferred(PendingType::kReadback, handle.slot.index);
		}
		else
		{
			m_ReadbackBuffers.release_slot(handle.slot.index);
		}
	}

	// No texture, sampler or view is ever valid here, so destroying one is destroying an invalid
	// handle, as on every backend.
	void
	ResourceManager::DestroyTexture(TextureHandle handle, bool deferred) noexcept
	{
		(void)deferred;
		core::ensure(ValidTextureHandle(handle), "Cannot destroy invalid texture handle");
	}

	void
	ResourceManager::DestroySampler(SamplerHandle handle, bool deferred) noexcept
	{
		(void)deferred;
		core::ensure(ValidSamplerHandle(handle), "Cannot destroy invalid sampler handle");
	}

	void
	ResourceManager::DestroySrv(SrvHandle handle, bool deferred) noexcept
	{
		(void)deferred;
		core::ensure(ValidSrvHandle(handle), "Cannot destroy invalid SRV handle");
	}

	void
	ResourceManager::DestroyRtv(RtvHandle handle, bool deferred) noexcept
	{
		(void)deferred;
		core::ensure(ValidRtvHandle(handle), "Cannot destroy invalid RTV handle");
	}

	void
	ResourceManager::DestroyDsv(DsvHandle handle, bool deferred) noexcept
	{
		(void)deferred;
		core::ensure(ValidDsvHandle(handle), "Cannot destroy invalid DSV handle");
	}

	void
	ResourceManager::RegisterQueue(ICommandQueue* queue) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		core::ensure(queue != nullptr, "RegisterQueue requires a non-null queue");
		core::ensure(
			m_RegisteredQueues.size() < c_MaxRegisteredQueues,
			"More than c_MaxRegisteredQueues submission timelines registered");
		m_RegisteredQueues.push_back(queue);
	}

	void
	ResourceManager::UnregisterQueue(ICommandQueue* queue) noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		for (uint32_t i = 0; i < m_RegisteredQueues.size(); ++i)
		{
			if (m_RegisteredQueues[i] == queue)
			{
				m_RegisteredQueues[i] = m_RegisteredQueues.back();
				m_RegisteredQueues.pop_back();
				break;
			}
		}

		// The queue has drained, so it gates nothing; dropping it from every gate keeps a freed
		// queue's address from aliasing a later one's.
		for (PendingDeletionBatch& batch : m_PendingBatches)
		{
			for (uint32_t i = 0; i < batch.gate.size(); ++i)
			{
				if (batch.gate[i].queue == queue)
				{
					batch.gate[i] = batch.gate.back();
					batch.gate.pop_back();
					break;
				}
			}
		}
	}

	DeletionGate
	ResourceManager::CaptureGate() const noexcept
	{
		auto gate = DeletionGate();
		for (ICommandQueue* queue : m_RegisteredQueues)
			gate.push_back({ queue, queue->GetNextFenceValue() });
		return gate;
	}

	void
	ResourceManager::RetireDeferred(
		const PendingType type,
		const uint32_t    slotIndex,
		const uint32_t    descriptorIndex) noexcept
	{
		const DeletionGate gate = CaptureGate();
		if (m_PendingBatches.empty() || !std::ranges::equal(m_PendingBatches.back().gate, gate))
			m_PendingBatches.push_back({ gate, {} });
		m_PendingBatches.back().deletions.push_back({ type, slotIndex, descriptorIndex });
	}

	void
	ResourceManager::CleanupExpiredResources() noexcept
	{
		const std::lock_guard lock(m_PoolMutex);
		auto                  completed = core::static_vector<QueueGate, c_MaxRegisteredQueues>();
		for (ICommandQueue* queue : m_RegisteredQueues)
			completed.push_back({ queue, queue->PollCurrentFenceValue() });

		const auto isCleared = [&](const QueueGate& entry) {
			for (const auto& [queue, value] : completed)
			{
				if (queue == entry.queue)
					return value >= entry.fenceValue;
			}
			// No longer registered: its owner flushed it before letting it go.
			return true;
		};

		const auto reclaim = [&](const PendingDeletion& pending) {
			switch (pending.type)
			{
			case PendingType::kBuffer:
				m_Buffers.reclaim_slot(pending.slotIndex);
				m_Table.Free(pending.descriptorIndex);
				break;
			case PendingType::kBufferSrv:
				m_BufferSrvs.reclaim_slot(pending.slotIndex);
				m_Table.Free(pending.descriptorIndex);
				break;
			case PendingType::kBufferUav:
				m_BufferUavs.reclaim_slot(pending.slotIndex);
				m_Table.Free(pending.descriptorIndex);
				break;
			case PendingType::kReadback:
				m_ReadbackBuffers.reclaim_slot(pending.slotIndex);
				break;
			case PendingType::kInvalid:
				core::fatal("A pending deletion was recorded with no resource type");
			}
		};

		std::erase_if(m_PendingBatches, [&](const PendingDeletionBatch& batch) {
			if (!std::ranges::all_of(batch.gate, isCleared))
				return false;
			for (const PendingDeletion& pending : batch.deletions) reclaim(pending);
			return true;
		});
	}

	const Rtv&
	ResourceManager::GetRtv(RtvHandle handle) const noexcept
	{
		(void)handle;
		NotOnVulkanYet("IResourceManager::GetRtv");
	}

	const Dsv&
	ResourceManager::GetDsv(DsvHandle handle) const noexcept
	{
		(void)handle;
		NotOnVulkanYet("IResourceManager::GetDsv");
	}

	TextureHandle
	ResourceManager::GetRtvTexture(RtvHandle handle) const noexcept
	{
		(void)handle;
		NotOnVulkanYet("IResourceManager::GetRtvTexture");
	}

	TextureHandle
	ResourceManager::GetDsvTexture(DsvHandle handle) const noexcept
	{
		(void)handle;
		NotOnVulkanYet("IResourceManager::GetDsvTexture");
	}

	const Buffer&
	ResourceManager::GetBuffer(BufferHandle handle) const noexcept
	{
		core::ensure(ValidBufferHandle(handle), "GetBuffer of an invalid buffer handle");
		return m_Buffers[handle.slot];
	}

	BufferDesc
	ResourceManager::GetBufferDesc(BufferHandle handle) const noexcept
	{
		return GetBuffer(handle).GetDesc();
	}

	const Texture&
	ResourceManager::GetTexture(TextureHandle handle) const noexcept
	{
		(void)handle;
		NotOnVulkanYet("IResourceManager::GetTexture");
	}

	TextureDesc
	ResourceManager::GetTextureDesc(TextureHandle handle) const noexcept
	{
		(void)handle;
		NotOnVulkanYet("IResourceManager::GetTextureDesc");
	}

	const Sampler&
	ResourceManager::GetSampler(SamplerHandle handle) const noexcept
	{
		(void)handle;
		NotOnVulkanYet("IResourceManager::GetSampler");
	}

	const ReadbackBuffer&
	ResourceManager::GetReadbackBuffer(ReadbackBufferHandle handle) const noexcept
	{
		core::ensure(
			ValidReadbackBufferHandle(handle),
			"GetReadbackBuffer of an invalid readback handle");
		return m_ReadbackBuffers[handle.slot.index];
	}

	TextureReadbackLayout
	ResourceManager::GetTextureReadbackLayout(TextureHandle handle) const noexcept
	{
		(void)handle;
		NotOnVulkanYet("IResourceManager::GetTextureReadbackLayout");
	}

	const void*
	ResourceManager::MapReadback(ReadbackBufferHandle handle) noexcept
	{
		return GetReadbackBuffer(handle).Map();
	}

	void
	ResourceManager::UnmapReadback(ReadbackBufferHandle handle) noexcept
	{
		// Mapped for its whole life; nothing to give back.
		core::ensure(
			ValidReadbackBufferHandle(handle),
			"UnmapReadback of an invalid readback handle");
	}

	bool
	ResourceManager::ValidBufferHandle(const BufferHandle& handle) const noexcept
	{
		return m_Buffers.valid(handle.slot) && !m_Buffers[handle.slot].IsNull();
	}

	bool
	ResourceManager::ValidReadbackBufferHandle(const ReadbackBufferHandle& handle) const noexcept
	{
		return m_ReadbackBuffers.valid(handle.slot) &&
		       !m_ReadbackBuffers[handle.slot.index].IsNull();
	}

	bool
	ResourceManager::ValidTextureHandle(const TextureHandle& handle) const noexcept
	{
		(void)handle;
		return false;
	}

	bool
	ResourceManager::IsTextureCube(const TextureHandle& handle) const noexcept
	{
		(void)handle;
		return false;
	}

	bool
	ResourceManager::ValidSrvHandle(const SrvHandle& handle) const noexcept
	{
		(void)handle;
		return false;
	}

	bool
	ResourceManager::ValidSamplerHandle(const SamplerHandle& handle) const noexcept
	{
		(void)handle;
		return false;
	}

	bool
	ResourceManager::ValidRtvHandle(const RtvHandle& handle) const noexcept
	{
		(void)handle;
		return false;
	}

	bool
	ResourceManager::ValidDsvHandle(const DsvHandle& handle) const noexcept
	{
		(void)handle;
		return false;
	}

	void
	ResourceManager::ClearRtv(ICommandList* cmdList, RtvHandle handle, float clearVal[4]) noexcept
	{
		(void)cmdList;
		(void)handle;
		(void)clearVal;
		NotOnVulkanYet("IResourceManager::ClearRtv");
	}

	void
	ResourceManager::ClearDsv(
		ICommandList* cmdList,
		DsvHandle     handle,
		float         depth,
		uint8_t       stencil) noexcept
	{
		(void)cmdList;
		(void)handle;
		(void)depth;
		(void)stencil;
		NotOnVulkanYet("IResourceManager::ClearDsv");
	}
}
