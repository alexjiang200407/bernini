#pragma once
#include "metal_cpp.h"

#include "cmd/CommandList.h"
#include "resource/ResourceManager.h"

#include <core/ref/RefCounter.h>
#include <core/ref/SharedRef.h>

namespace bgl
{
	/**
	 * The Metal command list. This slice records only buffer uploads and buffer->readback copies onto
	 * a single MTL::CommandBuffer built per Open(); the compute/mesh/texture path lands with the
	 * render slices and is gunimplemented for now.
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
		CopyBufferToReadback(ReadbackBufferHandle dst, BufferHandle src) noexcept override;

		void
		CopyTextureToReadback(ReadbackBufferHandle dst, TextureHandle src) noexcept override;

		// Clears a texture by running an empty render pass with a clear load action. Not on the RHI
		// interface -- ResourceManager::ClearRtv drives it (Metal has no free-standing clear).
		void
		ClearRenderTarget(MTL::Texture* texture, const float clearVal[4]) noexcept;

		// Clears a depth texture by running an empty render pass with a depth clear load action.
		// ResourceManager::ClearDsv drives it, mirroring ClearRenderTarget.
		void
		ClearDepth(MTL::Texture* texture, float depth) noexcept;

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
		WriteTexture(TextureHandle, std::span<const TextureSubresourceData>) noexcept override
		{
			gunimplemented(k);
		}

		void
		SetMeshletState(const MeshletState& gfxState) noexcept override;

		void
		DispatchMesh(
			uint32_t threadGroupCountX,
			uint32_t threadGroupCountY,
			uint32_t threadGroupCountZ) noexcept override;

		void
		DispatchMeshIndirect(uint32_t argIdx) noexcept override;

		void
		SetComputeState(const ComputeState& computeState) noexcept override;

		void
		Dispatch(
			uint32_t threadGroupCountX,
			uint32_t threadGroupCountY,
			uint32_t threadGroupCountZ) noexcept override;

	private:
		// Opens a render encoder for the current MeshletState -- render pass from the framebuffer,
		// pipeline, per-stage cbuffer binding, viewport/scissor -- leaving only the draw call to the
		// caller. Shared by the direct and indirect DispatchMesh paths.
		[[nodiscard]] MTL::RenderCommandEncoder*
		BeginMeshRenderPass() noexcept;

		static constexpr const char* k =
			"Metal CommandList: not implemented yet (render/scene slice)";

		CommandListDesc    m_Desc;
		ResourceManagerRef m_ResourceManager;
		MTL::Device*       m_Device = nullptr;

		NS::SharedPtr<MTL::CommandBuffer> m_CmdBuffer;
		NS::SharedPtr<NS::AutoreleasePool>
					 m_ScopePool;  // drains at Close; scopes Open..Close temporaries
		ComputeState m_ComputeState;
		MeshletState m_MeshletState;
		bool         m_Open = false;
	};
}
