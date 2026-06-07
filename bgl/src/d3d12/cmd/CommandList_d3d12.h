#pragma once
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/Texture.h"
#include "types/GraphicsState.h"
#include "types/QueueType.h"
#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	class CommandList : public core::RefCounter<ICommandList>
	{
	public:
		CommandList(
			QueueType             type,
			ICommandAllocator*    commandAllocator,
			ResourceManagerHandle resourceManager);

		~CommandList() noexcept;

		void
		WriteBuffer(BufferHandle handle, const void* data, size_t offset, size_t byteSize) override;

		void
		Open(ICommandAllocator* allocator) override;

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
			return m_Type;
		}

	private:
		QueueType                               m_Type;
		ResourceManagerHandle                   m_ResourceManager;
		wrl::ComPtr<ID3D12GraphicsCommandList7> m_CommandList;
		wrl::ComPtr<ID3D12Resource>             m_StagingBuffer;
		std::optional<GraphicsState>            m_CurrentGraphicsState;
		bool                                    m_Open = false;
	};
}
