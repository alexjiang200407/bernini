#include "cmd/CommandList_wgpu.h"

#include "cmd/CommandQueue_wgpu.h"
#include "resource/Buffer_wgpu.h"
#include "resource/ReadbackBuffer_wgpu.h"
#include "resource/ResourceManager.h"

namespace bgl
{
	CommandList::CommandList(
		WGPUDevice             device,
		const CommandListDesc& desc,
		ResourceManagerRef     resourceManager) noexcept :
		m_Device(device), m_Desc(desc), m_ResourceManager(std::move(resourceManager))
	{
		gassert(m_Device != nullptr, "CommandList: null device");
		gassert(m_ResourceManager != nullptr, "CommandList: null resource manager");

		wgpuDeviceAddRef(m_Device);
	}

	CommandList::~CommandList() noexcept
	{
		if (m_CommandBuffer != nullptr)
			wgpuCommandBufferRelease(m_CommandBuffer);
		if (m_Encoder != nullptr)
			wgpuCommandEncoderRelease(m_Encoder);

		wgpuDeviceRelease(m_Device);
	}

	void
	CommandList::Open(ICommandQueue* cmdQueue, ICommandAllocator*) noexcept
	{
		gassert(!IsOpen(), "Open: the list is already open");
		gassert(cmdQueue != nullptr, "Open: null queue");

		// Same latching rule as D3D12: a list belongs to one queue for its whole life.
		auto* queue = static_cast<CommandQueue*>(cmdQueue)->GetHandle();
		gassert(
			m_BoundQueue == nullptr || m_BoundQueue == queue,
			"Open: a command list may not move between queues");
		m_BoundQueue = queue;

		gassert(m_CommandBuffer == nullptr, "Open: the previous recording was never submitted");

		m_Encoder = wgpuDeviceCreateCommandEncoder(m_Device, nullptr);
	}

	void
	CommandList::Close() noexcept
	{
		gassert(IsOpen(), "Close: the list is not open");

		m_CommandBuffer = wgpuCommandEncoderFinish(m_Encoder, nullptr);

		wgpuCommandEncoderRelease(m_Encoder);
		m_Encoder = nullptr;
	}

	WGPUCommandBuffer
	CommandList::TakeCommandBuffer() noexcept
	{
		return std::exchange(m_CommandBuffer, nullptr);
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
			wgpuQueueWriteBuffer(m_BoundQueue, buffer.GetHandle(), gpuBufferOffset, data, byteSize);
			return;
		}

		gassert(gpuBufferOffset % 4 == 0, "WriteBuffer: the destination offset must be 4-aligned");

		auto padded = std::vector<std::byte>((byteSize + 3) & ~size_t{ 3 });
		std::memcpy(padded.data(), data, byteSize);

		wgpuQueueWriteBuffer(
			m_BoundQueue,
			buffer.GetHandle(),
			gpuBufferOffset,
			padded.data(),
			padded.size());
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

		wgpuCommandEncoderCopyBufferToBuffer(
			m_Encoder,
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

	void
	CommandList::Dispatch(uint32_t, uint32_t, uint32_t) noexcept
	{
		gfatal("Dispatch: compute pipelines are not implemented on the WebGPU backend yet");
	}

	void
	CommandList::BeginEvent(std::string_view name) noexcept
	{
		if (IsOpen())
			wgpuCommandEncoderPushDebugGroup(m_Encoder, wgpu::ToStringView(name));
	}

	void
	CommandList::EndEvent() noexcept
	{
		if (IsOpen())
			wgpuCommandEncoderPopDebugGroup(m_Encoder);
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
	CommandList::WriteTexture(TextureHandle, std::span<const TextureSubresourceData>) noexcept
	{
		gfatal("WriteTexture: textures are not implemented on the WebGPU backend yet");
	}

	void
	CommandList::CopyTextureToReadback(ReadbackBufferHandle, TextureHandle) noexcept
	{
		gfatal("CopyTextureToReadback: textures are not implemented on the WebGPU backend yet");
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
