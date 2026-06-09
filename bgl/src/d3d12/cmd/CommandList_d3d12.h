#pragma once
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/ResourceManager.h"
#include "resource/Texture.h"
#include "resource/UploadManager.h"
#include "types/GraphicsState.h"
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

		void
		WriteBuffer(BufferHandle handle, const void* data, size_t offset, size_t byteSize) override;

		void
		Open(ICommandQueue* cmdQueue, ICommandAllocator* allocator) override;

		void
		Close() override;

		void
		Barrier(BufferHandle handle, const BufferBarrierDesc& barrier) override;

		void
		Barrier(TextureHandle handle, const TextureBarrierDesc& barrier) override;

		void
		Barrier(RtvHandle handle, const TextureBarrierDesc& barrier) override;

		void
		SetGraphicsState(const GraphicsState& gfxState) override;

		void
		DrawInstanced(uint32_t vertexCount, uint32_t instanceCount) const override;

		ID3D12CommandList*
		GetD3D12CommandList() const
		{
			return m_CommandList.Get();
		}

		bool
		IsOpen() const override
		{
			return m_Open;
		}

		QueueType
		GetType() const override
		{
			return m_Desc.type;
		}

		void
		SubmitChunks(ICommandQueue* cmdQueue);

	private:
		CommandListDesc       m_Desc;
		ResourceManagerHandle m_ResourceManager;

		// Must be deleted after UploadManager
		wrl::ComPtr<ID3D12Resource> m_CurrentUploadBuffer = nullptr;
		UploadManager               m_UploadManager;

		wrl::ComPtr<ID3D12GraphicsCommandList7> m_CommandList;
		std::optional<GraphicsState>            m_CurrentGraphicsState;
		uint64_t                                m_LastCompletedFence = 0;
		uint64_t                                m_RecordingVersion   = 0;
		bool                                    m_Open               = false;
	};
}
