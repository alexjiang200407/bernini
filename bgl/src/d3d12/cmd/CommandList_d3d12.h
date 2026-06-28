#pragma once
#include "cmd/CommandList.h"
#include "pipeline/ComputePipeline_d3d12.h"
#include "pipeline/MeshletPipeline_d3d12.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "resource/Texture.h"
#include "resource/UploadManager.h"
#include "types/ComputeState.h"
#include "types/MeshletState.h"
#include "types/QueueType.h"
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class ICommandQueue;

	class CommandList : public core::RefCounter<ICommandList>
	{
	public:
		CommandList(
			const CommandListDesc& desc,
			ICommandAllocator*     commandAllocator,
			ResourceManagerHandle  resourceManager);
		~CommandList() noexcept override { logger::trace("~CommandList"); }

		CommandList(const CommandList&) noexcept = delete;
		CommandList(CommandList&&) noexcept      = delete;

		CommandList&
		operator=(const CommandList&) noexcept = delete;

		CommandList&
		operator=(CommandList&&) noexcept = delete;

		void
		WriteBuffer(BufferHandle handle, const void* data, size_t offset, size_t byteSize) noexcept
			override;

		void
		CopyBufferToReadback(ReadbackBufferHandle dst, BufferHandle src) noexcept override;

		void
		CopyTextureToReadback(ReadbackBufferHandle dst, TextureHandle src) noexcept override;

		void
		Open(ICommandQueue* cmdQueue, ICommandAllocator* allocator) noexcept override;

		void
		Close() noexcept override;

		void
		BeginEvent(std::string_view name) noexcept override;

		void
		EndEvent() noexcept override;

		void
		Barrier(BufferHandle handle, const BufferBarrierDesc& barrier) noexcept override;

		void
		Barrier(TextureHandle handle, const TextureBarrierDesc& barrier) noexcept override;

		void
		Barrier(RtvHandle handle, const TextureBarrierDesc& barrier) noexcept override;

		void
		Barrier(DsvHandle handle, const TextureBarrierDesc& barrier) noexcept override;

		void
		Barrier(
			std::span<const BufferHandle>      handles,
			std::span<const BufferBarrierDesc> barriers) noexcept override;

		void
		Barrier(
			std::span<const TextureHandle>      handles,
			std::span<const TextureBarrierDesc> barriers) noexcept override;

		void
		SetMeshletState(const MeshletState& gfxState) noexcept override;

		void
		DispatchMesh(
			uint32_t threadGroupCountX,
			uint32_t threadGroupCountY,
			uint32_t threadGroupCountZ) noexcept override;

		void
		SetComputeState(const ComputeState& computeState) noexcept override;

		void
		Dispatch(
			uint32_t threadGroupCountX,
			uint32_t threadGroupCountY,
			uint32_t threadGroupCountZ) noexcept override;

		ID3D12CommandList*
		GetD3D12CommandList() const noexcept
		{
			return m_CommandList.Get();
		}

		bool
		IsOpen() const noexcept override
		{
			return m_Open;
		}

		QueueType
		GetType() const noexcept override
		{
			return m_Desc.type;
		}

		void
		SubmitChunks(ICommandQueue* cmdQueue) noexcept;

	private:
		// Uploads the uniform bytes to a transient CBV and binds it. `compute` selects
		// the compute vs graphics root signature for the bind.
		void
		BindUniforms(const Uniforms& uniforms, bool compute) noexcept;

		CommandListDesc       m_Desc;
		ResourceManagerHandle m_ResourceManager;

		// Must be deleted after UploadManager
		wrl::ComPtr<ID3D12Resource> m_CurrentUploadBuffer = nullptr;
		UploadManager               m_UploadManager;

		wrl::ComPtr<ID3D12GraphicsCommandList7> m_CommandList;
		std::optional<MeshletState>             m_CurrentMeshletState;
		std::optional<ComputeState>             m_CurrentComputeState;
		uint64_t                                m_LastCompletedFence = 0;
		uint64_t                                m_RecordingVersion   = 0;
		bool                                    m_Open               = false;
	};
}
