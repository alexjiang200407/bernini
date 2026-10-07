#include "cmd/CommandList_vulkan.h"
#include "cmd/CommandAllocator_vulkan.h"
#include "cmd/CommandQueue_vulkan.h"
#include "cmd/TimestampHeap_vulkan.h"
#include "cmd/UploadRing_vulkan.h"
#include "convert_vulkan.h"
#include "native_device_vulkan.h"
#include "pipeline/ComputePipeline_vulkan.h"
#include "resource/BindlessTable_vulkan.h"
#include "resource/Buffer_vulkan.h"
#include "resource/ReadbackBuffer_vulkan.h"
#include "resource/ResourceManager_vulkan.h"
#include "volk_vulkan.h"
#include "vulkan_util.h"
#include <algorithm>
#include <array>
#include <bgpu/cmd/CommandAllocator.h>
#include <bgpu/cmd/CommandList.h>
#include <bgpu/cmd/CommandQueue.h>
#include <bgpu/cmd/TimestampHeap.h>
#include <bgpu/pipeline/ComputeKernel.h>
#include <bgpu/resource/Buffer.h>
#include <bgpu/resource/Dsv.h>
#include <bgpu/resource/Readback.h>
#include <bgpu/resource/ResourceManager.h>
#include <bgpu/resource/Rtv.h>
#include <bgpu/resource/Texture.h>
#include <bgpu/types/Barrier.h>
#include <bgpu/types/ComputeState.h>
#include <bgpu/types/MeshletState.h>
#include <bgpu/uniforms/DescriptorHandle.h>
#include <core/err/util.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bgpu
{
	namespace
	{
		// What a staged copy's source offset is aligned to; any multiple of 4 serves a buffer copy.
		constexpr uint64_t c_CopyAlignment = 16;
	}

	CommandList::CommandList(const CommandListDesc& desc, ResourceManagerRef resourceManager) :
		m_Desc(desc), m_ResourceManager(std::move(resourceManager)),
		m_UploadRing(m_ResourceManager->As<ResourceManager>()->GetContext(), desc.uploadChunkSize)
	{
		const GpuContextRef& context = m_ResourceManager->As<ResourceManager>()->GetContext();

		auto properties = VkPhysicalDeviceProperties();
		vkGetPhysicalDeviceProperties(GetVulkanHandles(*context).physicalDevice, &properties);
		m_UniformAlignment =
			std::max<uint64_t>(properties.limits.minUniformBufferOffsetAlignment, c_CopyAlignment);
	}

	CommandList::~CommandList() noexcept { spdlog::trace("~CommandList"); }

	const Buffer&
	CommandList::GetBuffer(const BufferHandle handle) const noexcept
	{
		return m_ResourceManager->GetBuffer(handle);
	}

	void
	CommandList::Open(ICommandQueue* cmdQueue, ICommandAllocator* allocator) noexcept
	{
		core::ensure(!m_Open, "Command list is already open");
		core::ensure(cmdQueue != nullptr, "Command queue cannot be null");
		core::ensure(allocator != nullptr, "Command allocator cannot be null");
		core::ensure(
			m_BoundQueue == nullptr || m_BoundQueue == cmdQueue,
			"A command list must always be opened with the queue that first opened it");
		m_BoundQueue = cmdQueue;

		const auto*    queue  = cmdQueue->As<CommandQueue>();
		const uint32_t family = queue->GetFamily();

		const GpuContextRef& context = m_ResourceManager->As<ResourceManager>()->GetContext();
		m_TimestampValidBits         = GetVulkanQueueFamilies(*context)[family].timestampValidBits;

		m_Allocator     = allocator->As<CommandAllocator>();
		m_CommandBuffer = m_Allocator->TakeCommandBuffer(family);

		auto begin  = VkCommandBufferBeginInfo();
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		EnsureVk(vkBeginCommandBuffer(m_CommandBuffer, &begin), "vkBeginCommandBuffer");

		// D3D12's state decay at an ExecuteCommandLists boundary: nothing before this recording is
		// left unordered against it.
		auto barrier          = VkMemoryBarrier2();
		barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		barrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
		barrier.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

		auto dependency               = VkDependencyInfo();
		dependency.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.memoryBarrierCount = 1;
		dependency.pMemoryBarriers    = &barrier;
		vkCmdPipelineBarrier2(m_CommandBuffer, &dependency);

		m_LastCompletedFence = cmdQueue->GetLastCompletedFence();
		m_UploadRing.BeginRecording();
		m_Open = true;
	}

	void
	CommandList::Close() noexcept
	{
		core::ensure(m_Open, "Command list must be open before closing");
		core::ensure(m_TimingPool == VK_NULL_HANDLE, "Command list closed with a timed span open");

		EnsureVk(vkEndCommandBuffer(m_CommandBuffer), "vkEndCommandBuffer");
		m_ComputeState.reset();
		m_Open = false;
	}

	void
	CommandList::Submitted(const uint64_t fenceValue) noexcept
	{
		m_UploadRing.Submitted(fenceValue);
	}

	void
	CommandList::WriteBuffer(
		const BufferHandle handle,
		const void*        data,
		const size_t       gpuBufferOffset,
		const size_t       byteSize) noexcept
	{
		core::ensure(m_Open, "WriteBuffer on a closed command list");
		if (byteSize == 0)
			return;

		const Buffer& buffer = GetBuffer(handle);
		core::ensure(
			gpuBufferOffset + byteSize <= buffer.GetDesc().byteSize,
			"WriteBuffer range exceeds the buffer");

		const UploadRing::Allocation staged =
			m_UploadRing.Allocate(byteSize, c_CopyAlignment, m_LastCompletedFence);
		std::memcpy(staged.cpu, data, byteSize);

		auto region      = VkBufferCopy();
		region.srcOffset = staged.offset;
		region.dstOffset = gpuBufferOffset;
		region.size      = byteSize;
		vkCmdCopyBuffer(m_CommandBuffer, staged.buffer, buffer.GetVkBuffer(), 1, &region);
	}

	void
	CommandList::CopyBuffer(
		const BufferHandle dst,
		const BufferHandle src,
		const uint64_t     dstOffset,
		const uint64_t     srcOffset,
		const uint64_t     byteSize) noexcept
	{
		if (byteSize == 0)
			return;

		const Buffer& dstBuffer = GetBuffer(dst);
		const Buffer& srcBuffer = GetBuffer(src);
		core::ensure(
			dstOffset + byteSize <= dstBuffer.GetDesc().byteSize,
			"CopyBuffer destination range exceeds the buffer");
		core::ensure(
			srcOffset + byteSize <= srcBuffer.GetDesc().byteSize,
			"CopyBuffer source range exceeds the buffer");

		auto region      = VkBufferCopy();
		region.srcOffset = srcOffset;
		region.dstOffset = dstOffset;
		region.size      = byteSize;
		vkCmdCopyBuffer(
			m_CommandBuffer,
			srcBuffer.GetVkBuffer(),
			dstBuffer.GetVkBuffer(),
			1,
			&region);
	}

	void
	CommandList::CopyBufferToReadback(
		const ReadbackBufferHandle dst,
		const BufferHandle         src) noexcept
	{
		const Buffer&         srcBuffer = GetBuffer(src);
		const ReadbackBuffer& readback  = m_ResourceManager->GetReadbackBuffer(dst);
		const uint64_t        byteSize  = srcBuffer.GetDesc().byteSize;
		core::ensure(
			readback.GetByteSize() >= byteSize,
			"Readback buffer is too small for the source buffer");

		auto region = VkBufferCopy();
		region.size = byteSize;
		vkCmdCopyBuffer(
			m_CommandBuffer,
			srcBuffer.GetVkBuffer(),
			readback.GetVkBuffer(),
			1,
			&region);
	}

	void
	CommandList::Barrier(const BufferHandle handle, const BufferBarrierDesc& barrier) noexcept
	{
		Barrier(
			std::span<const BufferHandle>(&handle, 1),
			std::span<const BufferBarrierDesc>(&barrier, 1));
	}

	void
	CommandList::Barrier(
		const std::span<const BufferHandle>      handles,
		const std::span<const BufferBarrierDesc> barriers) noexcept
	{
		core::ensure(
			handles.size() == barriers.size(),
			"Barrier handle/desc spans must match in size");
		if (handles.empty())
			return;

		auto vkBarriers = std::vector<VkBufferMemoryBarrier2>();
		vkBarriers.reserve(handles.size());
		for (size_t i = 0; i < handles.size(); ++i)
		{
			const BufferBarrierDesc& desc = barriers[i];

			auto barrier                = VkBufferMemoryBarrier2();
			barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
			barrier.srcStageMask        = ConvertBarrierSync(desc.syncBefore);
			barrier.srcAccessMask       = ConvertBarrierAccess(desc.accessBefore);
			barrier.dstStageMask        = ConvertBarrierSync(desc.syncAfter);
			barrier.dstAccessMask       = ConvertBarrierAccess(desc.accessAfter);
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer              = GetBuffer(handles[i]).GetVkBuffer();
			barrier.offset              = 0;
			barrier.size                = VK_WHOLE_SIZE;
			vkBarriers.push_back(barrier);
		}

		auto dependency                     = VkDependencyInfo();
		dependency.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(vkBarriers.size());
		dependency.pBufferMemoryBarriers    = vkBarriers.data();
		vkCmdPipelineBarrier2(m_CommandBuffer, &dependency);
	}

	void
	CommandList::Barrier(
		const std::span<const TextureHandle>      handles,
		const std::span<const TextureBarrierDesc> barriers) noexcept
	{
		core::ensure(
			handles.size() == barriers.size(),
			"Barrier handle/desc spans must match in size");
		if (!handles.empty())
			NotOnVulkanYet("ICommandList::Barrier on a texture");
	}

	void
	CommandList::Barrier(const TextureHandle handle, const TextureBarrierDesc& barrier) noexcept
	{
		(void)handle;
		(void)barrier;
		NotOnVulkanYet("ICommandList::Barrier on a texture");
	}

	void
	CommandList::Barrier(const RtvHandle handle, const TextureBarrierDesc& barrier) noexcept
	{
		(void)handle;
		(void)barrier;
		NotOnVulkanYet("ICommandList::Barrier on a render target");
	}

	void
	CommandList::Barrier(const DsvHandle handle, const TextureBarrierDesc& barrier) noexcept
	{
		(void)handle;
		(void)barrier;
		NotOnVulkanYet("ICommandList::Barrier on a depth target");
	}

	void
	CommandList::WriteTexture(
		const TextureHandle                           handle,
		const std::span<const TextureSubresourceData> subresources) noexcept
	{
		(void)handle;
		(void)subresources;
		NotOnVulkanYet("ICommandList::WriteTexture");
	}

	void
	CommandList::CopyTextureToReadback(
		const ReadbackBufferHandle dst,
		const TextureHandle        src) noexcept
	{
		(void)dst;
		(void)src;
		NotOnVulkanYet("ICommandList::CopyTextureToReadback");
	}

	void
	CommandList::BeginEvent(const std::string_view name) noexcept
	{
		if (vkCmdBeginDebugUtilsLabelEXT == nullptr)
			return;

		const auto terminated = std::string(name);
		auto       label      = VkDebugUtilsLabelEXT();
		label.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		label.pLabelName      = terminated.c_str();
		vkCmdBeginDebugUtilsLabelEXT(m_CommandBuffer, &label);
	}

	void
	CommandList::EndEvent() noexcept
	{
		if (vkCmdEndDebugUtilsLabelEXT != nullptr)
			vkCmdEndDebugUtilsLabelEXT(m_CommandBuffer);
	}

	void
	CommandList::BeginTiming(
		ITimestampHeap& heap,
		const uint32_t  startSlot,
		const uint32_t  endSlot) noexcept
	{
		core::ensure(m_Open, "BeginTiming on a closed command list");
		core::ensure(m_TimingPool == VK_NULL_HANDLE, "BeginTiming while a timed span is open");
		core::ensure(
			startSlot < heap.GetCapacity() && endSlot < heap.GetCapacity(),
			"BeginTiming slot outside the heap");
		core::ensure(m_TimestampValidBits > 0, "This queue's family cannot write timestamps");

		m_TimingPool    = heap.As<TimestampHeap>()->GetVkQueryPool();
		m_TimingEndSlot = endSlot;

		vkCmdResetQueryPool(m_CommandBuffer, m_TimingPool, startSlot, 1);
		vkCmdResetQueryPool(m_CommandBuffer, m_TimingPool, endSlot, 1);
		vkCmdWriteTimestamp2(
			m_CommandBuffer,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			m_TimingPool,
			startSlot);
	}

	bool
	CommandList::EndTiming() noexcept
	{
		core::ensure(m_TimingPool != VK_NULL_HANDLE, "EndTiming without a timed span open");

		vkCmdWriteTimestamp2(
			m_CommandBuffer,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
			m_TimingPool,
			m_TimingEndSlot);
		m_TimingPool = VK_NULL_HANDLE;
		return true;
	}

	void
	CommandList::ResolveTimestamps(
		ITimestampHeap& heap,
		const uint32_t  first,
		const uint32_t  count) noexcept
	{
		core::ensure(m_Open, "ResolveTimestamps on a closed command list");
		core::ensure(first + count <= heap.GetCapacity(), "ResolveTimestamps outside the heap");
		if (count == 0)
			return;

		// The copy writes a query only once it is available, so the writes it copies must be done:
		// without the wait flag, a slot no span wrote is skipped rather than waited on forever.
		auto barrier          = VkMemoryBarrier2();
		barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
		barrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.dstStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;

		auto dependency               = VkDependencyInfo();
		dependency.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependency.memoryBarrierCount = 1;
		dependency.pMemoryBarriers    = &barrier;
		vkCmdPipelineBarrier2(m_CommandBuffer, &dependency);

		const auto* timestamps = heap.As<TimestampHeap>();
		vkCmdCopyQueryPoolResults(
			m_CommandBuffer,
			timestamps->GetVkQueryPool(),
			first,
			count,
			timestamps->GetReadbackVkBuffer(),
			static_cast<VkDeviceSize>(first) * sizeof(uint64_t),
			sizeof(uint64_t),
			VK_QUERY_RESULT_64_BIT);
	}

	void
	CommandList::SetMeshletState(const MeshletState& gfxState) noexcept
	{
		(void)gfxState;
		NotOnVulkanYet("ICommandList::SetMeshletState");
	}

	void
	CommandList::DispatchMesh(
		const uint32_t threadGroupCountX,
		const uint32_t threadGroupCountY,
		const uint32_t threadGroupCountZ) noexcept
	{
		(void)threadGroupCountX;
		(void)threadGroupCountY;
		(void)threadGroupCountZ;
		NotOnVulkanYet("ICommandList::DispatchMesh");
	}

	void
	CommandList::DispatchMeshIndirect(const uint32_t argIdx) noexcept
	{
		(void)argIdx;
		NotOnVulkanYet("ICommandList::DispatchMeshIndirect");
	}

	void
	CommandList::DispatchMeshIndirectCount(const uint32_t argIdx, const uint32_t countIdx) noexcept
	{
		(void)argIdx;
		(void)countIdx;
		NotOnVulkanYet("ICommandList::DispatchMeshIndirectCount");
	}

	void
	CommandList::SetComputeState(const ComputeState& computeState) noexcept
	{
		m_ComputeState = computeState;
	}

#if defined(BERNINI_GPU_DEBUG)
	void
	CommandList::SetActiveDebugBuffer(const BufferHandle handle) noexcept
	{
		m_ActiveDebugBuffer = handle;
	}
#endif

	void
	CommandList::WriteConstants(
		const VkDescriptorSet set,
		const uint32_t        binding,
		const void*           data,
		const size_t          bytes) noexcept
	{
		core::ensure(bytes > 0, "A constant buffer has at least one byte");
		const UploadRing::Allocation staged =
			m_UploadRing.Allocate(bytes, m_UniformAlignment, m_LastCompletedFence);
		std::memcpy(staged.cpu, data, bytes);

		auto bufferInfo   = VkDescriptorBufferInfo();
		bufferInfo.buffer = staged.buffer;
		bufferInfo.offset = staged.offset;
		bufferInfo.range  = bytes;

		auto write            = VkWriteDescriptorSet();
		write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet          = set;
		write.dstBinding      = binding;
		write.descriptorCount = 1;
		write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.pBufferInfo     = &bufferInfo;
		vkUpdateDescriptorSets(
			GetVulkanHandles(*m_ResourceManager->As<ResourceManager>()->GetContext()).device,
			1,
			&write,
			0,
			nullptr);
	}

	void
	CommandList::Dispatch(
		const uint32_t threadGroupCountX,
		const uint32_t threadGroupCountY,
		const uint32_t threadGroupCountZ) noexcept
	{
		core::ensure(m_ComputeState.has_value(), "Compute state must be set before dispatch");
		core::ensure(
			m_ComputeState->kernel != nullptr && m_ComputeState->kernel->pipeline.IsInitialized(),
			"Compute kernel must be set in compute state");

		const ComputeKernel& kernel   = *m_ComputeState->kernel;
		const auto*          pipeline = kernel.pipeline->As<ComputePipeline>();
		const auto           bindings = pipeline->GetConstantBufferBindings();

		vkCmdBindPipeline(
			m_CommandBuffer,
			VK_PIPELINE_BIND_POINT_COMPUTE,
			pipeline->GetVkPipeline());

		auto sets = std::array<VkDescriptorSet, 2>{
			VK_NULL_HANDLE,
			m_ResourceManager->As<ResourceManager>()->GetBindlessSet(),
		};
		uint32_t firstSet = BindlessTable::c_Set;

		if (!bindings.empty())
		{
			sets[0]  = m_Allocator->AllocateSet(pipeline->GetConstantsSetLayout());
			firstSet = 0;

			for (const auto& [name, uniforms] : kernel.uniforms)
			{
				const uint32_t binding = bindings[uniforms.GetRootParamIndex()];
				const size_t   size    = uniforms.GetSize();

#if defined(BERNINI_GPU_DEBUG)
				// The active assertion buffer stands in for the kernel's own gDebug, whose mirror is
				// const here: the handle is written over the head of a copy of it.
				if (name == "gDebug" && !m_ActiveDebugBuffer.IsNull())
				{
					auto bytes = std::vector<std::byte>(
						static_cast<const std::byte*>(uniforms.Data()),
						static_cast<const std::byte*>(uniforms.Data()) + size);
					const auto handle = DescriptorHandle(m_ActiveDebugBuffer.bindlessIndex);
					bytes.resize(std::max(bytes.size(), sizeof(handle)));
					std::memcpy(bytes.data(), &handle, sizeof(handle));
					WriteConstants(sets[0], binding, bytes.data(), bytes.size());
					continue;
				}
#endif
				WriteConstants(sets[0], binding, uniforms.Data(), size);
			}
		}

		vkCmdBindDescriptorSets(
			m_CommandBuffer,
			VK_PIPELINE_BIND_POINT_COMPUTE,
			pipeline->GetVkPipelineLayout(),
			firstSet,
			static_cast<uint32_t>(sets.size()) - firstSet,
			sets.data() + firstSet,
			0,
			nullptr);

		vkCmdDispatch(m_CommandBuffer, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	}
}
