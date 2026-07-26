#include "cmd/CommandList_wgpu.h"

#include "cmd/CommandQueue_wgpu.h"
#include "pipeline/ComputeKernel.h"
#include "pipeline/ComputePipeline_wgpu.h"
#include "resource/Buffer_wgpu.h"
#include "resource/ReadbackBuffer_wgpu.h"
#include "resource/ResourceManager.h"
#include "resource/ResourceManager_wgpu.h"
#include "resource/Texture_wgpu.h"
#include "uniforms/Uniforms.h"

#include <core/math.h>

namespace bgl
{
	CommandList::CommandList(
		const wgpu::Device&    device,
		const CommandListDesc& desc,
		ResourceManagerRef     resourceManager) noexcept :
		m_Device(device), m_Desc(desc), m_ResourceManager(std::move(resourceManager))
	{
		gassert(m_Device != nullptr, "CommandList: null device");
		gassert(m_ResourceManager != nullptr, "CommandList: null resource manager");
	}

	void
	CommandList::Open(ICommandQueue* cmdQueue, ICommandAllocator*) noexcept
	{
		gassert(!IsOpen(), "Open: the list is already open");
		gassert(cmdQueue != nullptr, "Open: null queue");

		// Same latching rule as D3D12: a list belongs to one queue for its whole life.
		const wgpu::Queue& queue = static_cast<CommandQueue*>(cmdQueue)->GetHandle();
		gassert(
			m_BoundQueue == nullptr || m_BoundQueue.Get() == queue.Get(),
			"Open: a command list may not move between queues");
		m_BoundQueue = queue;

		gassert(m_CommandBuffer == nullptr, "Open: the previous recording was never submitted");

		m_Encoder = m_Device.CreateCommandEncoder();
	}

	void
	CommandList::Close() noexcept
	{
		gassert(IsOpen(), "Close: the list is not open");

		m_CommandBuffer = m_Encoder.Finish();
		m_Encoder       = nullptr;
	}

	wgpu::CommandBuffer
	CommandList::TakeCommandBuffer() noexcept
	{
		return std::move(m_CommandBuffer);
	}

	void
	CommandList::WriteBuffer(
		BufferHandle handle,
		const void*  data,
		size_t       gpuBufferOffset,
		size_t       byteSize) noexcept
	{
		gassert(m_ResourceManager->ValidBufferHandle(handle), "WriteBuffer: invalid handle");
		gassert(data != nullptr, "WriteBuffer: null data");

		const auto& buffer = m_ResourceManager->GetBuffer(handle);
		gassert(
			gpuBufferOffset + byteSize <= buffer.GetByteSize(),
			"WriteBuffer: the range does not fit the buffer");

		// WebGPU requires both the destination offset and the size to be 4-byte multiples, so a
		// ragged tail cannot be written directly; the caller's data is padded into a staging copy.
		if (byteSize % 4 == 0 && gpuBufferOffset % 4 == 0)
		{
			m_BoundQueue.WriteBuffer(buffer.GetHandle(), gpuBufferOffset, data, byteSize);
			return;
		}

		gassert(gpuBufferOffset % 4 == 0, "WriteBuffer: the destination offset must be 4-aligned");

		auto padded = std::vector<std::byte>(core::align(byteSize, 4));
		std::memcpy(padded.data(), data, byteSize);

		m_BoundQueue.WriteBuffer(buffer.GetHandle(), gpuBufferOffset, padded.data(), padded.size());
	}

	void
	CommandList::CopyBuffer(
		BufferHandle dst,
		BufferHandle src,
		uint64_t     dstOffset,
		uint64_t     srcOffset,
		uint64_t     byteSize) noexcept
	{
		if (byteSize == 0)
			return;

		gassert(IsOpen(), "CopyBuffer: the list is not open");
		gassert(
			m_ResourceManager->ValidBufferHandle(dst),
			"CopyBuffer: invalid destination handle");
		gassert(m_ResourceManager->ValidBufferHandle(src), "CopyBuffer: invalid source handle");

		const auto& source      = m_ResourceManager->GetBuffer(src);
		const auto& destination = m_ResourceManager->GetBuffer(dst);

		gassert(
			srcOffset + byteSize <= source.GetByteSize(),
			"CopyBuffer: the source range does not fit the buffer");
		gassert(
			dstOffset + byteSize <= destination.GetByteSize(),
			"CopyBuffer: the destination range does not fit the buffer");

		m_Encoder.CopyBufferToBuffer(
			source.GetHandle(),
			srcOffset,
			destination.GetHandle(),
			dstOffset,
			byteSize);
	}

	void
	CommandList::CopyBufferToReadback(ReadbackBufferHandle dst, BufferHandle src) noexcept
	{
		gassert(IsOpen(), "CopyBufferToReadback: the list is not open");
		gassert(
			m_ResourceManager->ValidReadbackBufferHandle(dst),
			"CopyBufferToReadback: invalid readback handle");
		gassert(
			m_ResourceManager->ValidBufferHandle(src),
			"CopyBufferToReadback: invalid source handle");

		const auto& source   = m_ResourceManager->GetBuffer(src);
		const auto& readback = m_ResourceManager->GetReadbackBuffer(dst);

		gassert(
			readback.GetByteSize() >= source.GetByteSize(),
			"CopyBufferToReadback: the readback buffer is too small");

		m_Encoder.CopyBufferToBuffer(
			source.GetHandle(),
			0,
			readback.GetHandle(),
			0,
			source.GetByteSize());
	}

	void
	CommandList::SetComputeState(const ComputeState& computeState) noexcept
	{
		m_CurrentComputeState = computeState;
	}

	namespace
	{
		// Reads each resource leaf's slot index out of the Uniforms bytes and emits its bind-group
		// entry. Offsets are struct-relative, so baseOffset accumulates down wrapper structs the same
		// way Uniforms::Traverse does when the handle was written.
		void
		CollectBindings(
			const ReflectedLayout&             layout,
			const std::byte*                   data,
			uint32_t                           baseOffset,
			const ResourceManager&             resources,
			std::vector<wgpu::BindGroupEntry>& entries)
		{
			for (const ReflectedField& field : layout.fields)
			{
				if (field.layout.isResourceHandle)
				{
					uint32_t slotIndex = 0;
					std::memcpy(&slotIndex, data + baseOffset + field.offset, sizeof(uint32_t));

					const wgpu::Buffer& buffer = resources.GetBufferBindingBySlotIndex(slotIndex);

					auto entry    = wgpu::BindGroupEntry{};
					entry.binding = field.binding;
					entry.buffer  = buffer;
					entry.size    = buffer.GetSize();
					entries.push_back(entry);
				}
				else if (field.layout.kind == UniformType::kStruct)
				{
					CollectBindings(
						field.layout,
						data,
						baseOffset + field.offset,
						resources,
						entries);
				}
			}
		}
	}

	void
	CommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z) noexcept
	{
		gassert(IsOpen(), "Dispatch: the list is not open");
		gassert(m_CurrentComputeState.has_value(), "Dispatch: compute state must be set");

		const ComputeKernel* kernel = m_CurrentComputeState->kernel;
		gassert(
			kernel != nullptr && kernel->pipeline.IsInitialized(),
			"Dispatch: compute kernel must be set in compute state");

		auto*       pipeline  = kernel->pipeline->As<ComputePipeline>();
		const auto& resources = *static_cast<ResourceManager*>(m_ResourceManager.Get());

		auto entries = std::vector<wgpu::BindGroupEntry>();
		for (const auto& [name, uniforms] : kernel->uniforms)
		{
			const UniformLayoutEntry entry = pipeline->GetUniformLayoutEntry(name);
			CollectBindings(
				*entry.layout,
				static_cast<const std::byte*>(uniforms.Data()),
				0,
				resources,
				entries);
		}

		auto bgDesc       = wgpu::BindGroupDescriptor{};
		bgDesc.layout     = pipeline->GetBindGroupLayout();
		bgDesc.entryCount = entries.size();
		bgDesc.entries    = entries.data();
		auto bindGroup    = m_Device.CreateBindGroup(&bgDesc);

		auto pass = m_Encoder.BeginComputePass();
		pass.SetPipeline(pipeline->GetPipeline());
		pass.SetBindGroup(0, bindGroup);
		pass.DispatchWorkgroups(x, y, z);
		pass.End();
	}

	void
	CommandList::BeginEvent(std::string_view name) noexcept
	{
		if (IsOpen())
			m_Encoder.PushDebugGroup(name);
	}

	void
	CommandList::EndEvent() noexcept
	{
		if (IsOpen())
			m_Encoder.PopDebugGroup();
	}

	// WebGPU tracks resource usage and inserts its own transitions, so the FrameGraph's derived
	// barriers have nothing to do here. They stay callable because pass code calls them blind.
	void
	CommandList::Barrier(BufferHandle, const BufferBarrierDesc&) noexcept
	{}

	void
	CommandList::Barrier(TextureHandle, const TextureBarrierDesc&) noexcept
	{}

	void
	CommandList::Barrier(RtvHandle, const TextureBarrierDesc&) noexcept
	{}

	void
	CommandList::Barrier(DsvHandle, const TextureBarrierDesc&) noexcept
	{}

	void
	CommandList::Barrier(
		std::span<const BufferHandle>      handles,
		std::span<const BufferBarrierDesc> barriers) noexcept
	{
		gassert(handles.size() == barriers.size(), "Barrier: handle and barrier counts differ");
	}

	void
	CommandList::Barrier(
		std::span<const TextureHandle>      handles,
		std::span<const TextureBarrierDesc> barriers) noexcept
	{
		gassert(handles.size() == barriers.size(), "Barrier: handle and barrier counts differ");
	}

	void
	CommandList::WriteTexture(
		TextureHandle                           handle,
		std::span<const TextureSubresourceData> subresources) noexcept
	{
		gassert(m_ResourceManager->ValidTextureHandle(handle), "WriteTexture: invalid handle");
		gassert(!subresources.empty(), "WriteTexture: no subresource data");

		const auto& texture = m_ResourceManager->GetTexture(handle);
		const auto& desc    = texture.GetDesc();

		// Mip 0, array layer 0: the round-trip path uploads one subresource. Full mip/array upload
		// lands with the material atlas (W4).
		const TextureSubresourceData& sub = subresources.front();

		auto dst    = wgpu::TexelCopyTextureInfo{};
		dst.texture = texture.GetHandle();
		dst.aspect  = wgpu::TextureAspect::All;

		auto layout         = wgpu::TexelCopyBufferLayout{};
		layout.bytesPerRow  = static_cast<uint32_t>(sub.rowPitch);
		layout.rowsPerImage = desc.height;

		const auto extent = wgpu::Extent3D{ desc.width, desc.height, 1 };

		m_BoundQueue.WriteTexture(
			&dst,
			sub.data,
			sub.slicePitch != 0 ? sub.slicePitch : sub.rowPitch * desc.height,
			&layout,
			&extent);
	}

	void
	CommandList::CopyTextureToReadback(ReadbackBufferHandle dst, TextureHandle src) noexcept
	{
		gassert(IsOpen(), "CopyTextureToReadback: the list is not open");
		gassert(
			m_ResourceManager->ValidReadbackBufferHandle(dst),
			"CopyTextureToReadback: invalid readback handle");
		gassert(
			m_ResourceManager->ValidTextureHandle(src),
			"CopyTextureToReadback: invalid texture handle");

		const auto& texture  = m_ResourceManager->GetTexture(src);
		const auto& desc     = texture.GetDesc();
		const auto& readback = m_ResourceManager->GetReadbackBuffer(dst);
		const auto  rb       = m_ResourceManager->GetTextureReadbackLayout(src);

		gassert(
			readback.GetByteSize() >= rb.totalBytes,
			"CopyTextureToReadback: the readback buffer is too small");

		auto source    = wgpu::TexelCopyTextureInfo{};
		source.texture = texture.GetHandle();
		source.aspect  = wgpu::TextureAspect::All;

		auto destination                = wgpu::TexelCopyBufferInfo{};
		destination.buffer              = readback.GetHandle();
		destination.layout.bytesPerRow  = static_cast<uint32_t>(rb.rowPitch);
		destination.layout.rowsPerImage = desc.height;

		const auto extent = wgpu::Extent3D{ desc.width, desc.height, 1 };
		m_Encoder.CopyTextureToBuffer(&source, &destination, &extent);
	}

	void
	CommandList::SetMeshletState(const MeshletState&) noexcept
	{
		gfatal("SetMeshletState: WebGPU has no mesh shaders");
	}

	void
	CommandList::DispatchMesh(uint32_t, uint32_t, uint32_t) noexcept
	{
		gfatal("DispatchMesh: WebGPU has no mesh shaders");
	}

	void
	CommandList::DispatchMeshIndirect(uint32_t) noexcept
	{
		gfatal("DispatchMeshIndirect: WebGPU has no mesh shaders");
	}
}
