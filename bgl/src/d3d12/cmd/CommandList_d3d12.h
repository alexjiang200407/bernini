#pragma once
#include "cmd/CommandList.h"
#include "resource/Buffer.h"
#include "resource/Texture.h"
#include "types/GraphicsState.h"
#include "types/QueueType.h"

namespace bgl
{
	class CommandListImpl
	{
	public:
		CommandListImpl(
			QueueType        type,
			CommandAllocator commandAllocator,
			ResourceManager  resourceManager);

		void
		WriteBuffer(BufferHandle handle, const void* data, size_t offset, size_t byteSize);

		void
		Open(CommandAllocator allocator);

		void
		Close();

		void
		Barrier(BufferHandle handle, const BufferBarrierDesc& barrier);

		void
		Barrier(TextureHandle handle, const TextureBarrierDesc& barrier);

		void
		Barrier(RtvHandle handle, const TextureBarrierDesc& barrier);

		void
		SetGraphicsState(const GraphicsState& gfxState);

		void
		DrawInstanced(uint32_t vertexCount, uint32_t instanceCount) const;

		ID3D12CommandList*
		GetCommandList() const
		{
			return m_CommandList.Get();
		}

	private:
		QueueType                               m_Type;
		ResourceManager                         m_ResourceManager;
		wrl::ComPtr<ID3D12GraphicsCommandList7> m_CommandList;
		std::optional<GraphicsState>            m_CurrentGraphicsState;
		bool                                    m_Open = false;

		friend class CommandList;
	};
}
