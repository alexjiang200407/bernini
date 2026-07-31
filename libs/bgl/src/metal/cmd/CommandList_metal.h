#pragma once
#include "metal_cpp.h"

#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	/**
	 * The Metal command list: one MTL::CommandBuffer per Open(), recorded through encoders.
	 *
	 * Metal has no free-form recording. A blit, a compute dispatch and a draw each need their own
	 * encoder and only one may be open, so the list opens an encoder on demand and ends it when a
	 * command needs a different kind -- or, for a draw, a different FrameBuffer. Consecutive draws to
	 * one target therefore share a render pass, which on a tile GPU is the difference between one
	 * load/store cycle and one per draw.
	 *
	 * Barriers are no-ops: Metal hazard-tracks resources referenced within one command buffer, so the
	 * FrameGraph's upload->copy ordering holds without explicit fences.
	 */
	class CommandList final : public core::RefCounter<ICommandList>
	{
	public:
		CommandList(
			const CommandListDesc& desc,
			ICommandAllocator*     commandAllocator,
			ResourceManagerRef     resourceManager);

		void
		WriteBuffer(
			BufferHandle handle,
			const void*  data,
			size_t       gpuBufferOffset,
			size_t       byteSize) noexcept override;

		void
		CopyBuffer(
			BufferHandle dst,
			BufferHandle src,
			uint64_t     dstOffset,
			uint64_t     srcOffset,
			uint64_t     byteSize) noexcept override;

		void
		CopyBufferToReadback(ReadbackBufferHandle dst, BufferHandle src) noexcept override;

		void
		CopyTextureToReadback(ReadbackBufferHandle dst, TextureHandle src) noexcept override;

		// Clears a texture by running an empty render pass with a clear load action. Not on the RHI
		// interface -- ResourceManager::ClearRtv drives it (Metal has no free-standing clear).
		void
		ClearRenderTarget(MTL::Texture* texture, const float clearVal[4]) noexcept;

		// As ClearRenderTarget, for depth: Metal has no free-standing clear, so an empty pass with a
		// Clear load action is the clear. Driven by ResourceManager::ClearDsv.
		void
		ClearDepthStencil(MTL::Texture* texture, float depth, uint8_t stencil) noexcept;

		void
		Open(ICommandQueue* cmdQueue, ICommandAllocator* allocator) noexcept override;

		void
		Close() noexcept override;

		void
		BeginEvent(std::string_view name) noexcept override;

		void
		EndEvent() noexcept override;

		[[nodiscard]] bool
		IsOpen() const noexcept override
		{
			return m_Open;
		}

		[[nodiscard]] QueueType
		GetType() const noexcept override
		{
			return m_Desc.type;
		}

		// Consumed by CommandQueue::ExecuteCommandList to signal the fence and commit.
		[[nodiscard]] MTL::CommandBuffer*
		GetCommandBuffer() const noexcept
		{
			return m_CmdBuffer.get();
		}

		// ---- barriers: handled implicitly by Metal within a command buffer ----

		void
		Barrier(BufferHandle, const BufferBarrierDesc&) noexcept override
		{}
		void
		Barrier(TextureHandle, const TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(RtvHandle, const TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(DsvHandle, const TextureBarrierDesc&) noexcept override
		{}
		void
		Barrier(std::span<const BufferHandle>, std::span<const BufferBarrierDesc>) noexcept override
		{}
		void
		Barrier(std::span<const TextureHandle>, std::span<const TextureBarrierDesc>) noexcept
			override
		{}

		// ---- not yet implemented (render + scene slices) ----

		void
		WriteTexture(
			TextureHandle                           handle,
			std::span<const TextureSubresourceData> subresources) noexcept override;
		void
		DispatchMeshIndirect(uint32_t) noexcept override
		{
			gunimplemented(c_Unimplemented);
		}

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

	private:
		enum class EncoderKind
		{
			kNone,
			kBlit,
			kCompute,
			kRender,
		};

		// Ends whatever encoder is open. Every path that needs a different one goes through here, so
		// the "only one open at a time" rule holds without each call site remembering it.
		void
		EndEncoder() noexcept;

		[[nodiscard]] MTL::BlitCommandEncoder*
		BlitEncoder() noexcept;

		[[nodiscard]] MTL::ComputeCommandEncoder*
		ComputeEncoder() noexcept;

		// Reopens when `fb` differs from the pass in flight: a render encoder is bound to its
		// attachments, so a draw to a different target cannot share it.
		[[nodiscard]] MTL::RenderCommandEncoder*
		RenderEncoder(const FrameBuffer& fb) noexcept;

		static constexpr const char* c_Unimplemented =
			"Metal CommandList: not implemented yet (render/scene slice)";

		CommandListDesc    m_Desc;
		ResourceManagerRef m_ResourceManager;
		MTL::Device*       m_Device = nullptr;

		NS::SharedPtr<MTL::CommandBuffer> m_CmdBuffer;

		MTL::CommandEncoder* m_Encoder     = nullptr;
		EncoderKind          m_EncoderKind = EncoderKind::kNone;
		FrameBuffer          m_EncoderFrameBuffer;
		NS::SharedPtr<NS::AutoreleasePool>
					 m_ScopePool;  // drains at Close; scopes Open..Close temporaries
		ComputeState m_ComputeState;
		MeshletState m_MeshletState;
		bool         m_Open = false;
	};
}
